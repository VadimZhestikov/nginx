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
(function () {
  /* ── Slide narration — works around Chrome/Edge autoplay policy ───── *
   *                                                                      *
   *  Strategy: require ONE explicit user click ("Start Narration")       *
   *  to unlock audio for the session, then poll every 120 ms for the    *
   *  active slide. setInterval callbacks always run in a context where   *
   *  audio.play() is permitted after the initial click gesture.          *
   * ──────────────────────────────────────────────────────────────────── */

  /* Lazy Audio cache — objects created only when first needed */
  const AUDIO_DATA = {
__AUDIO_DATA__
  };

  let currentSlide  = null;
  let currentAudio  = null;
  let narrationOn   = true;
  let sessionActive = false;   // flips to true after the Start button click

  function getAudio(id) {
    const entry = AUDIO_DATA[id];
    if (!entry) return null;
    if (entry instanceof Audio) return entry;
    const a = new Audio('data:audio/mp3;base64,' + entry);
    AUDIO_DATA[id] = a;
    return a;
  }

  function stopCurrent() {
    if (currentAudio) {
      currentAudio.pause();
      currentAudio.currentTime = 0;
    }
  }

  function playSlide(id) {
    stopCurrent();
    if (!narrationOn) return;
    const a = getAudio(id);
    if (!a) return;
    currentAudio = a;
    const p = a.play();
    if (p && p.catch) p.catch(err => console.warn('Narration play failed:', err));
  }

  /* ── 🔊 / 🔇 toggle (shown after session starts) ─────────────────── */
  const toggleBtn = document.createElement('button');
  toggleBtn.id    = 'narration-toggle';
  toggleBtn.title = 'Toggle narration';
  toggleBtn.textContent = '🔊';
  Object.assign(toggleBtn.style, {
    display:      'none',          // hidden until session starts
    position:     'fixed',
    bottom:       '18px',
    right:        '22px',
    zIndex:       '99999',
    background:   'rgba(10,20,30,0.75)',
    color:        '#fff',
    border:       '1px solid rgba(255,255,255,0.35)',
    borderRadius: '50%',
    width:        '44px',
    height:       '44px',
    fontSize:     '20px',
    cursor:       'pointer',
    lineHeight:   '44px',
    textAlign:    'center',
    padding:      '0',
  });
  toggleBtn.addEventListener('click', () => {
    narrationOn = !narrationOn;
    toggleBtn.textContent = narrationOn ? '🔊' : '🔇';
    if (!narrationOn) stopCurrent();
    else if (currentSlide !== null) playSlide(currentSlide);
  });
  document.body.appendChild(toggleBtn);

  /* ── Start overlay ────────────────────────────────────────────────── */
  const overlay = document.createElement('div');
  Object.assign(overlay.style, {
    position:       'fixed',
    inset:          '0',
    zIndex:         '99998',
    display:        'flex',
    flexDirection:  'column',
    alignItems:     'center',
    justifyContent: 'center',
    gap:            '18px',
    background:     'rgba(5,10,20,0.78)',
    backdropFilter: 'blur(4px)',
  });

  const startBtn = document.createElement('button');
  startBtn.textContent = '▶  Start with Narration';
  Object.assign(startBtn.style, {
    padding:      '18px 40px',
    fontSize:     '1.3rem',
    fontFamily:   'inherit',
    background:   '#1a3a5c',
    color:        '#7ec8e3',
    border:       '2px solid #7ec8e3',
    borderRadius: '8px',
    cursor:       'pointer',
    letterSpacing: '0.04em',
  });

  const skipBtn = document.createElement('button');
  skipBtn.textContent = 'Continue without audio';
  Object.assign(skipBtn.style, {
    padding:      '8px 20px',
    fontSize:     '0.85rem',
    fontFamily:   'inherit',
    background:   'transparent',
    color:        '#78909c',
    border:       '1px solid #37474f',
    borderRadius: '6px',
    cursor:       'pointer',
  });

  overlay.appendChild(startBtn);
  overlay.appendChild(skipBtn);
  document.body.appendChild(overlay);

  function startSession(withAudio) {
    overlay.remove();
    sessionActive = true;
    narrationOn   = withAudio;
    toggleBtn.style.display = 'flex';
    toggleBtn.textContent   = withAudio ? '🔊' : '🔇';

    /* Determine which slide is currently active and play it */
    const active = document.querySelector('section.bespoke-marp-active');
    if (active) {
      currentSlide = parseInt(active.id, 10);
      if (withAudio) playSlide(currentSlide);
    }

    /* Poll for slide changes every 120 ms.
       setInterval callbacks run after the click gesture has unlocked audio,
       so audio.play() succeeds without any further user interaction. */
    setInterval(() => {
      if (!sessionActive) return;
      const el = document.querySelector('section.bespoke-marp-active');
      if (!el) return;
      const id = parseInt(el.id, 10);
      if (id !== currentSlide) {
        currentSlide = id;
        playSlide(id);
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
