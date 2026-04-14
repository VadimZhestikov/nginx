#!/usr/bin/env python3
"""
add_audio_to_slides.py

Reads From_Birds_Eye_View.md, extracts per-slide speaker notes,
generates MP3 audio with edge-tts (Microsoft Edge Neural TTS),
and embeds it into From_Birds_Eye_View.html so that each slide
auto-plays its narration when the slide becomes active.

Usage:
    python3 add_audio_to_slides.py
Output:
    From_Birds_Eye_View_audio.html
"""

import asyncio
import base64
import os
import re
import sys
import tempfile

import edge_tts

MD_PATH   = "From_Birds_Eye_View.md"
HTML_PATH = "From_Birds_Eye_View.html"
OUT_PATH  = "From_Birds_Eye_View_audio.html"
VOICE     = "en-US-AriaNeural"    # neural voice — clear, natural, presentation-grade

NUM_PRESENTATION_SLIDES = 26   # sections beyond this are the appendix / notes

# ── Parse slide bodies (for fallback narration) ──────────────────────────────

def parse_slide_bodies(md_path: str) -> dict[int, str]:
    """Return {slide_number: cleaned_text} for the first NUM_PRESENTATION_SLIDES slides."""
    with open(md_path, encoding="utf-8") as f:
        content = f.read()

    # Strip YAML front-matter
    content = re.sub(r"^---\n.*?\n---\n", "", content, flags=re.DOTALL)

    # Split on --- slide separators
    raw_slides = re.split(r"\n---\n", content)

    bodies: dict[int, str] = {}
    for i, raw in enumerate(raw_slides[:NUM_PRESENTATION_SLIDES], start=1):
        # Remove HTML comments (<!-- ... -->)
        text = re.sub(r"<!--.*?-->", "", raw, flags=re.DOTALL)
        # Remove fenced code blocks entirely (they sound terrible as TTS)
        text = re.sub(r"```[^\n]*\n.*?```", "[code example]", text, flags=re.DOTALL)
        # Remove Marp/AsciiDoc directives (lines starting with :)
        text = re.sub(r"^:.*$", "", text, flags=re.MULTILINE)
        # Remove markdown heading markers
        text = re.sub(r"^#{1,6}\s+", "", text, flags=re.MULTILINE)
        # Remove bold/italic markers
        text = re.sub(r"\*\*([^*]+)\*\*", r"\1", text)
        text = re.sub(r"\*([^*]+)\*", r"\1", text)
        # Remove backtick code spans
        text = re.sub(r"`([^`]*)`", r"\1", text)
        # Remove table formatting characters
        text = re.sub(r"\|", " ", text)
        text = re.sub(r"^[-=]{3,}$", "", text, flags=re.MULTILINE)
        # Remove blockquote markers
        text = re.sub(r"^>\s*", "", text, flags=re.MULTILINE)
        # Collapse excess whitespace
        text = re.sub(r"\n{3,}", "\n\n", text)
        text = text.strip()
        if text:
            bodies[i] = text
    return bodies


# ── Parse speaker notes from the ## Speaker Notes section ───────────────────

def parse_speaker_notes(md_path: str) -> dict[int, str]:
    with open(md_path, encoding="utf-8") as f:
        content = f.read()

    # Locate the "## Speaker Notes" block
    m = re.search(r"## Speaker Notes\n(.*?)(?=\n---|\Z)", content, re.DOTALL)
    if not m:
        print("WARNING: '## Speaker Notes' section not found in markdown.")
        return {}

    notes_block = m.group(1)

    notes: dict[int, str] = {}
    # Match  "### Slide N (…) — …\n<text until next ### Slide or ## heading>"
    pattern = re.compile(
        r"### Slide (\d+)[^\n]*\n(.*?)(?=\n### Slide \d+|\n## |\Z)",
        re.DOTALL,
    )
    for match in pattern.finditer(notes_block):
        slide_num = int(match.group(1))
        raw = match.group(2).strip()
        # Strip markdown emphasis markers, inline code, and extra blank lines
        raw = re.sub(r"`[^`]*`", lambda x: x.group(0)[1:-1], raw)
        raw = re.sub(r"\*\*([^*]+)\*\*", r"\1", raw)
        raw = re.sub(r"\*([^*]+)\*", r"\1", raw)
        raw = re.sub(r"\n{3,}", "\n\n", raw)
        notes[slide_num] = raw

    return notes


# ── Generate TTS audio ───────────────────────────────────────────────────────

async def tts_to_bytes(text: str, voice: str) -> bytes:
    """Return MP3 bytes for *text* using edge-tts."""
    communicate = edge_tts.Communicate(text, voice)
    # Collect all audio chunks
    chunks = []
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            chunks.append(chunk["data"])
    return b"".join(chunks)


async def generate_all_audio(notes: dict[int, str], voice: str) -> dict[int, str]:
    """
    Returns {slide_num: base64_mp3_string} for every slide that has a note.
    Slides are processed concurrently.
    """
    print(f"Generating audio for {len(notes)} slides using voice '{voice}' …")

    async def one(num: int, text: str) -> tuple[int, str]:
        print(f"  slide {num:2d} … ", end="", flush=True)
        mp3 = await tts_to_bytes(text, voice)
        b64 = base64.b64encode(mp3).decode("ascii")
        print(f"{len(mp3) // 1024} KB")
        return num, b64

    results = await asyncio.gather(*[one(n, t) for n, t in sorted(notes.items())])
    return dict(results)


# ── HTML post-processing ─────────────────────────────────────────────────────

AUDIO_JS = """\
<script>
/* ── Slide narration ──────────────────────────────────────────────────────── *
 * Base64 audio → Blob → blob: URL → Audio element.                           *
 * blob: URLs bypass Chrome/Edge restrictions on large data: URIs for media.  *
 * Audio unlocked by ONE explicit user click on the Start button.             *
 * ─────────────────────────────────────────────────────────────────────────── */
(function () {
  'use strict';

  /* Raw base64 MP3 strings, keyed by slide number */
  const B64 = {
__AUDIO_DATA__
  };

  /* ── helpers ── */
  function b64ToBlob(b64) {
    try {
      const bin  = atob(b64);
      const buf  = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) buf[i] = bin.charCodeAt(i);
      return new Blob([buf], { type: 'audio/mpeg' });
    } catch (e) {
      console.error('[narration] b64ToBlob failed', e);
      return null;
    }
  }

  /* Pre-convert all slides to blob: URLs on session start */
  const URLS  = {};   /* slide_id → blob: URL string */
  const CACHE = {};   /* slide_id → Audio object      */

  function preload() {
    console.log('[narration] preloading', Object.keys(B64).length, 'slides …');
    for (const [id, b64] of Object.entries(B64)) {
      const blob = b64ToBlob(b64);
      if (!blob) { console.warn('[narration] skip slide', id); continue; }
      URLS[id] = URL.createObjectURL(blob);
    }
    console.log('[narration] blob URLs ready:', Object.keys(URLS).length);
  }

  function getAudio(id) {
    if (CACHE[id]) return CACHE[id];
    const url = URLS[id];
    if (!url) { console.warn('[narration] no url for slide', id); return null; }
    const a = new Audio(url);
    a.addEventListener('error', e => console.error('[narration] audio error slide', id, a.error?.code, e));
    CACHE[id] = a;
    return a;
  }

  /* ── playback ── */
  let current = null;

  function stopCurrent() {
    if (current) { current.pause(); current.currentTime = 0; }
  }

  function playSlide(id) {
    stopCurrent();
    const a = getAudio(id);
    if (!a) return;
    current = a;
    console.log('[narration] playing slide', id);
    a.play().then(
      ()  => console.log('[narration] playing slide', id, '✓'),
      err => console.warn('[narration] play rejected slide', id, err.name, err.message)
    );
  }

  /* ── UI ── */
  let narrationOn   = true;
  let sessionActive = false;
  let currentSlide  = null;

  /* Toggle button */
  const toggleBtn = document.createElement('button');
  toggleBtn.id    = 'narration-toggle';
  toggleBtn.title = 'Toggle narration';
  Object.assign(toggleBtn.style, {
    display: 'none', position: 'fixed', bottom: '16px', right: '20px',
    zIndex: '2147483646', background: 'rgba(10,20,30,0.82)',
    color: '#fff', border: '1.5px solid rgba(255,255,255,0.4)',
    borderRadius: '50%', width: '46px', height: '46px',
    fontSize: '20px', cursor: 'pointer', lineHeight: '46px',
    textAlign: 'center', padding: '0', fontFamily: 'inherit',
  });
  toggleBtn.addEventListener('click', () => {
    narrationOn = !narrationOn;
    toggleBtn.textContent = narrationOn ? '🔊' : '🔇';
    if (!narrationOn) stopCurrent();
    else if (currentSlide !== null) playSlide(currentSlide);
  });
  document.body.appendChild(toggleBtn);

  /* Start overlay */
  const overlay = document.createElement('div');
  Object.assign(overlay.style, {
    position: 'fixed', top: '0', left: '0', right: '0', bottom: '0',
    zIndex: '2147483647', display: 'flex', flexDirection: 'column',
    alignItems: 'center', justifyContent: 'center', gap: '20px',
    background: 'rgba(5,10,20,0.82)',
  });

  function mkBtn(text, bg, fg, border) {
    const b = document.createElement('button');
    b.textContent = text;
    Object.assign(b.style, {
      padding: '16px 38px', fontSize: '1.25rem', fontFamily: 'inherit',
      background: bg, color: fg, border: '2px solid ' + border,
      borderRadius: '8px', cursor: 'pointer', letterSpacing: '0.03em',
    });
    return b;
  }

  const startBtn = mkBtn('▶  Start with Narration', '#0d2b45', '#7ec8e3', '#7ec8e3');
  const skipBtn  = mkBtn('Continue without audio',  'transparent', '#78909c', '#455a64');
  skipBtn.style.fontSize = '0.9rem';
  skipBtn.style.padding  = '10px 22px';

  overlay.append(startBtn, skipBtn);
  document.body.appendChild(overlay);

  function startSession(withAudio) {
    overlay.remove();
    sessionActive = true;
    narrationOn   = withAudio;
    toggleBtn.style.display = 'flex';
    toggleBtn.textContent   = withAudio ? '🔊' : '🔇';

    if (withAudio) preload();   /* convert base64 → blob URLs now, inside click handler */

    const active = document.querySelector('section.bespoke-marp-active');
    currentSlide  = active ? parseInt(active.id, 10) : 1;
    console.log('[narration] session started, current slide:', currentSlide);

    if (withAudio) playSlide(currentSlide);

    /* Poll for slide changes — setInterval runs in activated context */
    setInterval(() => {
      const el = document.querySelector('section.bespoke-marp-active');
      if (!el) return;
      const id = parseInt(el.id, 10);
      if (id !== currentSlide) {
        currentSlide = id;
        if (narrationOn) playSlide(id);
      }
    }, 120);
  }

  startBtn.addEventListener('click', () => startSession(true));
  skipBtn.addEventListener('click',  () => startSession(false));
})();
</script>
"""


def build_audio_data_js(audio_map: dict[int, str]) -> str:
    """Build the JS object literal with base64 strings."""
    lines = []
    for slide_num in sorted(audio_map.keys()):
        b64 = audio_map[slide_num]
        lines.append(f"    {slide_num}: {json_string(b64)},")
    return "\n".join(lines)


def json_string(s: str) -> str:
    """Return a JS string literal (double-quoted, no escaping needed for base64)."""
    return f'"{s}"'


def inject_into_html(html: str, audio_map: dict[int, str]) -> str:
    """Inject the audio JS block just before </body>."""
    data_js  = build_audio_data_js(audio_map)
    script   = AUDIO_JS.replace("__AUDIO_DATA__", data_js)
    return html.replace("</body>", script + "\n</body>", 1)


# ── Main ─────────────────────────────────────────────────────────────────────

async def amain():
    explicit_notes = parse_speaker_notes(MD_PATH)
    slide_bodies   = parse_slide_bodies(MD_PATH)

    # Merge: explicit notes take priority; fall back to slide body text
    notes: dict[int, str] = {}
    for i in range(1, NUM_PRESENTATION_SLIDES + 1):
        if i in explicit_notes:
            notes[i] = explicit_notes[i]
        elif i in slide_bodies:
            notes[i] = slide_bodies[i]

    if not notes:
        print("No narration text found — aborting.")
        sys.exit(1)

    print(f"Narration for {len(notes)} slides "
          f"({len(explicit_notes)} explicit notes + "
          f"{len(notes)-len(explicit_notes)} body fallbacks)")

    audio_map = await generate_all_audio(notes, VOICE)

    with open(HTML_PATH, encoding="utf-8") as f:
        html = f.read()

    enhanced = inject_into_html(html, audio_map)

    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(enhanced)

    kb = os.path.getsize(OUT_PATH) // 1024
    print(f"\nDone → {OUT_PATH}  ({kb} KB)")
    print("Open in a browser. Click 🔊 to toggle narration. Arrow keys to navigate.")


if __name__ == "__main__":
    asyncio.run(amain())
