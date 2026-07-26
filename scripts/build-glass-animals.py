#!/usr/bin/env python3
"""Split the glass_animals pack into two playable kits plus a loop pack.

The source mixes 16 one-shots with 14 pre-cut loops (and one byte-identical
duplicate). Both kinds go onto pads: triggering a loop as a chop is normal MPC
practice, and pads now hold up to 12.5 s so a 4-bar loop survives whole.

  kit A "glassdrums" - drums + rhythm loops
  kit B "glassfx"    - melodic, texture and FX material
  loop pack          - the same 14 loops as tempo-locked backing beds

Loops are NOT re-cut. Every duration already lands on a bar boundary to within
a millisecond, in two tempo families (2.730667 s/bar = 87.890625 BPM, and
3.072 s/bar = 78.125 BPM). A beat-grid fit disagreed with that arithmetic on
several files, and the arithmetic is exact, so the arithmetic wins.
"""
import subprocess, os, struct, numpy as np

SRC = "/Users/cypher/Downloads/glass_animals"
RATE = 22050
TARGET_PEAK = 0.891
MAX_PAD_FRAMES = 275625        # 12.5 s, matches CYPHER_TUNE_MAX_SAMPLE_FRAMES
FADE_IN_MS, FADE_OUT_MS = 1.0, 6.0
SILENCE_FLOOR = 0.004

# Two kits, each a mix of one-shots and loops.
# (stem, label, velocity, choke group, gain)
KITS = {
    "glassdrums": ("Glass Drums", [
        ("kick",               "Kick",     118, 0, 255),
        ("snare",              "Snare",    112, 0, 240),
        ("clap",               "Clap",     108, 2, 225),
        ("spring-clap",        "SprClap",  104, 2, 215),

        ("loud-kick",          "HardKick", 120, 0, 250),
        ("bigger-spring-clap", "BigClap",  106, 2, 220),
        ("fill",               "Fill",     108, 0, 225),
        ("four-on-the-floor",  "4OnFloor", 110, 1, 225),

        ("drum-loop",          "DrumLoop", 110, 1, 220),
        ("verse-drum-loop",    "VrsDrums", 110, 1, 220),
        ("hat-pattern",        "HatPatrn",  98, 3, 200),
        ("hat-loop",           "HatLoop",   98, 3, 200),

        ("shaker-loop",        "Shaker",    96, 3, 195),
        ("verse-backbeat",     "Backbeat", 106, 1, 215),
        ("808",                "808",      116, 4, 240),
        ("808-loop",           "808Loop",  116, 4, 240),
    ]),
    "glassfx": ("Glass FX", [
        ("bass-pluck",         "BassPlk",  112, 4, 230),
        ("guitar-strum",       "Guitar",   104, 0, 215),
        ("organ-stabs",        "Organ",    106, 0, 215),
        ("cymbal-roll",        "CymRoll",   96, 3, 195),

        ("reverse-clap",       "RevClap",  100, 2, 205),
        ("engine-rev",         "EngRev",    98, 0, 200),
        ("fx",                 "FX",        98, 0, 200),
        ("sfx-loop",           "SFXLoop",   98, 0, 200),

        ("vfx-1",              "VFX1",      96, 0, 195),
        ("vfx-2",              "VFX2",      96, 0, 195),
        ("vfx-3",              "VFX3",      96, 0, 195),
        ("crash-fx",           "CrashFX",  100, 0, 205),

        ("riser",              "Riser",     92, 0, 190),
        ("western-vox",        "Vox",       98, 0, 205),
    ]),
}

# name -> (title, bars, bpm), from exact duration arithmetic.
LOOPS = [
    ("drum-loop",         "Drum Loop",      4, 87.9),
    ("verse-drum-loop",   "Verse Drums",    4, 78.1),
    ("hat-pattern",       "Hat Pattern",    4, 87.9),
    ("hat-loop",          "Hat Loop",       2, 78.1),
    ("shaker-loop",       "Shaker",         2, 87.9),
    ("verse-backbeat",    "Verse Backbeat", 2, 78.1),
    ("four-on-the-floor", "Four On Floor",  1, 87.9),
    ("808",               "808",            4, 87.9),
    ("808-loop",          "808 Loop",       4, 78.1),
    ("organ-stabs",       "Organ Stabs",    4, 87.9),
    ("sfx-loop",          "SFX Loop",       4, 87.9),
    ("fx",                "FX",             2, 87.9),
    ("crash-fx",          "Crash FX",       4, 78.1),
    ("guitar-strum",      "Guitar Strum",   3, 87.9),
]

EXCLUDED = [("loud-kick (1).wav", "byte-identical duplicate of loud-kick.wav")]

_cache = {}


def decode(stem):
    if stem not in _cache:
        p = os.path.join(SRC, stem + ".wav")
        raw = subprocess.run(
            ["ffmpeg", "-v", "error", "-i", p, "-ac", "1", "-ar", str(RATE),
             "-f", "f32le", "-"], capture_output=True, check=True).stdout
        _cache[stem] = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    return _cache[stem].copy()


def normalize(x):
    peak = float(np.max(np.abs(x))) or 1e-9
    return x * (TARGET_PEAK / peak)


def to_i16(x):
    return np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16).tolist()


def write_wav(path, s16):
    data = struct.pack("<%dh" % len(s16), *s16)
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        fh.write(struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        fh.write(b"data" + struct.pack("<I", len(data)) + data)


def condition_pad(x):
    """Trim, cap, fade, normalize. Loops keep their exact length, so only the
    one-shots get their leading silence trimmed - shortening a loop by even a
    few ms would break its bar alignment."""
    x = x - x.mean()
    peak = float(np.max(np.abs(x))) or 1e-9
    thr = peak * SILENCE_FLOOR
    start = 0
    for j, v in enumerate(x):
        if abs(v) > thr:
            start = max(0, j - int(RATE * 0.0005))
            break
    x = x[start:]
    capped = len(x) > MAX_PAD_FRAMES
    if capped:
        x = x[:MAX_PAD_FRAMES]
    ni = max(1, int(RATE * FADE_IN_MS / 1000))
    for j in range(min(ni, len(x))):
        x[j] *= j / ni
    no = max(1, int(RATE * (40.0 if capped else FADE_OUT_MS) / 1000))
    for j in range(min(no, len(x))):
        x[len(x) - 1 - j] *= j / no
    return normalize(x), capped


LOOP_STEMS = {n for n, _, _, _ in LOOPS}

for kit, (title, pads) in KITS.items():
    d = f"out/kits/{kit}"
    os.makedirs(d, exist_ok=True)
    print(f"=== KIT {kit} ({title}) ===")
    print(f"{'pad':>3} {'label':<10}{'src':<22}{'kind':>7}{'sec':>7}{'KB':>7}")
    total = 0
    for i, (stem, label, vel, choke, gain) in enumerate(pads):
        x, capped = condition_pad(decode(stem))
        s16 = to_i16(x)
        write_wav(f"{d}/pad{i+1:02d}.wav", s16)
        total += len(s16) * 2
        kind = "loop" if stem in LOOP_STEMS else "1shot"
        print(f"{i+1:>3} {label:<10}{stem:<22}{kind:>7}{len(s16)/RATE:>7.2f}"
              f"{len(s16)*2/1024:>7.0f}{'  cap' if capped else ''}")
    with open(f"{d}/kit.txt", "w") as fh:
        fh.write(title + "\n")
    print(f"  {len(pads)} pads, {total/1024/1024:.2f} MB\n")

ld = "out/loops/glassanimals"
os.makedirs(ld, exist_ok=True)
print("=== LOOP PACK (tempo-locked beds) ===")
rows, lb = [], 0
for stem, title, bars, bpm in LOOPS:
    s16 = to_i16(normalize(decode(stem) - decode(stem).mean()))
    steps = bars * 16
    s16 = s16[:(len(s16) // steps) * steps]
    write_wav(f"{ld}/{stem}.wav", s16)
    lb += len(s16) * 2
    rows.append((stem, title, bpm, bars, len(s16)))
    print(f"  {stem:<20}{title:<16}{bpm:>6.1f} BPM {bars:>2} bars {len(s16)/RATE:>6.2f}s")
with open(f"{ld}/loops.txt", "w") as fh:
    for n, t, bpm, bars, fr in rows:
        fh.write(f"{n}\t{t}\t{bpm}\t{bars}\t{fr}\n")
with open(f"{ld}/pack.txt", "w") as fh:
    fh.write("Glass Animals\n")
print(f"  {len(rows)} loops, {lb/1024/1024:.2f} MB")

print("\n=== EXCLUDED ===")
for f, why in EXCLUDED:
    print(f"  {f:<24}{why}")
