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
/* ── Slide narration — Web Audio API ────────────────────────────────────────
 *  Flow:
 *  1. Page load  → build AudioContext + decode all MP3s (no play yet)
 *  2. User click → ctx.resume() + play slide 1
 *  3. setInterval(120ms) watches for active-slide change → play next slide
 *
 *  All AudioBuffers are decoded before the Start button is enabled, so there
 *  is no "awaiting decode" async gap when navigating between slides.
 * ─────────────────────────────────────────────────────────────────────────── */
(function () {
  'use strict';

  /* Raw base64 MP3 strings keyed by slide number */
  const B64 = {
__AUDIO_DATA__
  };

  /* ── AudioContext ── */
  /* Create eagerly (suspended state is fine before user gesture) */
  const ctx = new (window.AudioContext || window.webkitAudioContext)();
  console.log('[narration] AudioContext created, state:', ctx.state,
              'sampleRate:', ctx.sampleRate);

  /* ── Decode all slides up-front ── */
  function b64ToAB(b64) {
    const bin  = atob(b64);
    const ab   = new ArrayBuffer(bin.length);
    const view = new Uint8Array(ab);
    for (let i = 0; i < bin.length; i++) view[i] = bin.charCodeAt(i);
    return ab;
  }

  const BUFFERS = {};   /* numeric slide id → AudioBuffer once decoded */
  let   decodesDone = 0;
  const totalSlides = Object.keys(B64).length;

  const allDecoded = Promise.all(
    Object.entries(B64).map(([id, b64]) =>
      ctx.decodeAudioData(b64ToAB(b64))
        .then(buf => {
          BUFFERS[parseInt(id, 10)] = buf;
          decodesDone++;
          console.log('[narration] decoded slide', id,
                      buf.duration.toFixed(1) + 's',
                      '(' + decodesDone + '/' + totalSlides + ')');
          return buf;
        })
        .catch(err => {
          console.error('[narration] decode FAILED slide', id, err);
        })
    )
  );

  /* ── Playback ── */
  let currentSource = null;
  let narrationOn   = true;
  let currentSlide  = null;

  function stopCurrent() {
    if (currentSource) {
      try { currentSource.stop(0); } catch (_) {}
      currentSource.disconnect();
      currentSource = null;
    }
  }

  function playSlide(id) {
    console.log('[narration] playSlide(' + id + ')',
                'ctx.state=' + ctx.state,
                'buf=' + (BUFFERS[id] ? BUFFERS[id].duration.toFixed(1)+'s' : 'MISSING'));

    stopCurrent();
    if (!narrationOn) return;

    const buf = BUFFERS[id];
    if (!buf) {
      console.warn('[narration] no buffer for slide', id,
                   '— available:', Object.keys(BUFFERS).join(','));
      return;
    }

    /* Resume context (Chrome may suspend it when idle) then start source */
    ctx.resume().then(() => {
      console.log('[narration] ctx resumed, state=' + ctx.state);
      const src = ctx.createBufferSource();
      src.buffer = buf;
      src.connect(ctx.destination);
      src.onended = () => console.log('[narration] slide', id, 'ended');
      src.start(ctx.currentTime);   /* explicit currentTime — never "in the past" */
      currentSource = src;
      console.log('[narration] ▶ slide', id, 'started',
                  buf.duration.toFixed(1) + 's',
                  '@t=' + ctx.currentTime.toFixed(2));
    }).catch(err => console.error('[narration] resume failed', err));
  }

  /* ── Test beep (oscillator — no MP3 decode involved) ── */
  function playTestBeep() {
    ctx.resume().then(() => {
      const osc  = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.frequency.value = 440;
      gain.gain.value     = 0.35;
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start();
      osc.stop(ctx.currentTime + 0.5);
      console.log('[narration] beep state=' + ctx.state);
    });
  }

  /* ── UI helpers ── */
  function mkBtn(text, bg, fg, bd, fs, pad) {
    const b = document.createElement('button');
    b.textContent = text;
    Object.assign(b.style, {
      padding: pad, fontSize: fs, fontFamily: 'inherit',
      background: bg, color: fg, border: '2px solid ' + bd,
      borderRadius: '8px', cursor: 'pointer', letterSpacing: '0.03em',
    });
    return b;
  }

  /* ── Toggle button ── */
  const toggleBtn = document.createElement('button');
  Object.assign(toggleBtn.style, {
    display: 'none', position: 'fixed', bottom: '16px', right: '20px',
    zIndex: '2147483646', background: 'rgba(10,20,30,0.85)',
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

  /* ── Start overlay ── */
  const overlay = document.createElement('div');
  Object.assign(overlay.style, {
    position: 'fixed', top: '0', left: '0', right: '0', bottom: '0',
    zIndex: '2147483647', display: 'flex', flexDirection: 'column',
    alignItems: 'center', justifyContent: 'center', gap: '18px',
    background: 'rgba(5,10,20,0.85)',
  });

  const startBtn = mkBtn('▶  Start with Narration',
                         '#0d2b45', '#7ec8e3', '#7ec8e3', '1.25rem', '16px 38px');
  const beepBtn  = mkBtn('🎵  Test sound first',
                         '#1a2a1a', '#a5d6a7', '#4caf50', '0.95rem', '10px 24px');
  const skipBtn  = mkBtn('Continue without audio',
                         'transparent', '#78909c', '#455a64', '0.85rem', '8px 20px');

  const hint = document.createElement('p');
  hint.textContent = 'Click "Test sound" to confirm your speakers are working.';
  Object.assign(hint.style, {
    color: '#546e7a', fontSize: '0.8rem', fontFamily: 'inherit', margin: '0',
  });

  overlay.append(startBtn, beepBtn, hint, skipBtn);
  document.body.appendChild(overlay);

  beepBtn.addEventListener('click', () => {
    playTestBeep();
    hint.textContent = 'Heard a beep? → click "Start with Narration".';
    hint.style.color = '#80cbc4';
  });

  function startSession(withAudio) {
    overlay.remove();
    narrationOn = withAudio;
    toggleBtn.style.display  = 'flex';
    toggleBtn.textContent    = withAudio ? '🔊' : '🔇';

    const active = document.querySelector('section.bespoke-marp-active');
    currentSlide = active ? parseInt(active.id, 10) : 1;
    console.log('[narration] session started slide=' + currentSlide +
                ' audio=' + withAudio +
                ' decoded=' + decodesDone + '/' + totalSlides);

    if (withAudio) playSlide(currentSlide);

    /* Read the current slide number from multiple sources, most reliable first */
    function activeSlide() {
      /* 1. URL hash — Marp bespoke always sets #N on navigation */
      const m = window.location.hash.match(/^#(\d+)/);
      if (m) return parseInt(m[1], 10);
      /* 2. CSS class fallback */
      const el = document.querySelector('section.bespoke-marp-active');
      if (el && el.id) return parseInt(el.id, 10);
      return currentSlide || 1;
    }

    /* Log initial hash so we can verify detection works */
    console.log('[narration] initial hash:', window.location.hash || '(empty)',
                '→ slide', activeSlide());

    /* hashchange fires synchronously when bespoke updates the URL */
    window.addEventListener('hashchange', () => {
      const id = activeSlide();
      if (id !== currentSlide) {
        console.log('[narration] hashchange: ' + currentSlide + ' → ' + id);
        currentSlide = id;
        if (narrationOn) playSlide(id);
      }
    });

    /* setInterval as belt-and-suspenders backup (also keeps ctx alive) */
    setInterval(() => {
      if (narrationOn && ctx.state === 'suspended') ctx.resume().catch(() => {});
      const id = activeSlide();
      if (id !== currentSlide) {
        console.log('[narration] poll: ' + currentSlide + ' → ' + id);
        currentSlide = id;
        if (narrationOn) playSlide(id);
      }
    }, 120);
  }

  startBtn.addEventListener('click', () => startSession(true));
  skipBtn.addEventListener('click',  () => startSession(false));

  /* Log when all decodes finish */
  allDecoded.then(() =>
    console.log('[narration] all', totalSlides, 'slides decoded and ready')
  );
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
