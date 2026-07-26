#!/usr/bin/env python3
"""Cut the vol-2 sample packs into bar-aligned loops using the supplied
tracklists.

Same pipeline as the Revival pack: fit a beat grid to each track, find the
downbeat, cut a whole number of bars, crossfade the seam, normalize. The
difference is that several tracklists state a BPM - where they do, that value
seeds the grid search instead of a blind estimate, which removes the
octave-error risk entirely (blind detection kept latching onto the 8th-note
pulse and reporting double tempo).
"""
import subprocess, os, struct, numpy as np

RATE = 22050
TARGET_PEAK = 0.891
MAX_BARS = 8
MIN_BARS = 2
XFADE_MS = 8
HOP, WIN = 256, 1024

DL = "/Users/cypher/Downloads/samples-vol-2"

# (file, pack prefix, duration, [(start_s, title, bpm_hint_or_None), ...])
PACKS = [
    (f"{DL}/Polyphonic Music Library - Soul Affair Vol. 1  Soul Sample Pack.mp3",
     "sa", 258.1, [
         (0,   "Soul Affair",        72),
         (60,  "Holdin' On",         67),
         (147, "Since You Been Gone", None),
         (175, "Let Me Down",        None),
         (235, "Keep The Faith",     None),
     ]),
    (f"{DL}/100 Unreleased Soul & Gospel Samples for Drake, J. Cole, Kendrick Type Beats.mp3",
     "un", 334.3, [
         (0,   "Love Will Keep Us",  74),
         (120, "Rza's Layer",        81),
         (167, "While You Were Gone", 70),
         (222, "Pack Preview 1",     None),
         (282, "Pack Preview 2",     None),
     ]),
    (f"{DL}/Cinema Themes Vol. 1  Italian Film Score Sample Pack.mp3",
     "ct", 483.6, [
         (0,   "High Off You",       None),
         (54,  "Fuga Dall'Isola",    None),
         (140, "Notturno",           None),
         (194, "Le Milieu Criminal", None),
         (248, "Sergio's Escape",    None),
         (306, "Saint & The Sinner", None),
     ]),
    (f"{DL}/Polyphonic Music Library - Temptations Vol. 3  Deep Soul, Psych, Jazz Fusion Samples.mp3",
     "tm", 197.8, [
         (0,   "Wanted It All",      None),
         (57,  "Baby Your Love",     None),
         (114, "Mariana's Revenge",  None),
         (154, "All This Time",      None),
     ]),
    (f"{DL}/Soulful Sonics Vol. 6 - Soul & Gospel Sample Pack.mp3",
     "ss", 459.8, [
         (0,   "When You Need Me",   None),
         (43,  "Rise Again",         None),
         (90,  "Secrets",            None),
         (136, "Journey Ahead",      None),
         (180, "Reflections",        None),
         (228, "My Sunshine",        None),
         (275, "Hard Truths",        None),
         (318, "Cosmic Love",        None),
         (365, "The Slow Burn",      None),
         (414, "Love and War",       None),
     ]),
]


def decode(path, start, dur):
    cmd = ["ffmpeg", "-v", "error", "-ss", f"{start}", "-t", f"{dur}", "-i", path,
           "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").astype(np.float64)


def onset_env(x):
    n = 1 + (len(x) - WIN) // HOP
    if n < 8:
        return np.zeros(1)
    w = np.hanning(WIN)
    fr = np.lib.stride_tricks.as_strided(
        x, shape=(n, WIN), strides=(x.strides[0] * HOP, x.strides[0])) * w
    mag = np.abs(np.fft.rfft(fr, axis=1))
    flux = np.sum(np.maximum(np.diff(mag, axis=0), 0), axis=1)
    flux = np.maximum(flux - np.median(flux), 0)
    return flux / (flux.max() or 1.0)


def estimate_bpm(env, lo=58, hi=150):
    """Blind tempo estimate.

    The range has to reach ~150 because on several of these the strongest
    periodicity is the 8th-note pulse (137-144), not the quarter. Clipping the
    search at 100 doesn't avoid that - it just makes the search saturate at the
    ceiling and return a value near 98 that fits nothing. So search wide and
    fold anything above 105 down an octave: no sample in this material is
    genuinely faster than that, while 137-144 halves cleanly to the 68-72 the
    rest of the packs sit in.
    """
    if len(env) < 16:
        return 72.0
    v = env - env.mean()
    ac = np.correlate(v, v, mode="full")[len(v) - 1:]
    if ac[0] <= 0:
        return 72.0
    ac /= ac[0]
    fps = RATE / HOP
    best, best_v = 72.0, -1e9
    for bpm in np.arange(lo, hi + 0.25, 0.25):
        lag = fps * 60.0 / bpm
        i = int(round(lag))
        if 1 < i < len(ac):
            s = ac[i]
            for m in (2, 4):
                j = int(round(lag * m))
                if j < len(ac):
                    s += 0.5 * ac[j]
            if s > best_v:
                best_v, best = s, bpm
    while best > 105.0:
        best /= 2.0
    return best


def search_grid(env, lo=58.0, hi=105.0, step=0.25):
    """Find tempo and phase in one pass, scored by how much onset energy lands
    on the beat grid.

    This replaces an autocorrelation estimate followed by octave-folding. That
    two-stage approach kept trading one error for another: capping the search
    low made it saturate at the ceiling (four tracks pinned near 98), while
    opening it up let 4/3 artifacts near 112-117 win and get halved to a
    nonsense 56. Grid fit is the property actually wanted - a grid at twice the
    true tempo has to spend half its beats on weak off-beats, so it scores
    worse on its own.

    Mean-per-beat alone is still biased toward slow tempos, because a
    half-tempo grid only ever samples strong downbeats and so scores well on
    average while explaining half the music. A log-normal tempo prior centred
    on 75 BPM counteracts that. The prior is observed rather than arbitrary:
    across the 11 Revival loops and the tagged tracks here, every stated tempo
    falls between 62 and 90, so 75 +/- ~0.35 octaves is this material's actual
    distribution.
    """
    fps = RATE / HOP
    best = (-1e9, 72.0, 0.0)
    for bpm in np.arange(lo, hi + step, step):
        period = fps * 60.0 / bpm
        if period < 2:
            continue
        prior = float(np.exp(-0.5 * (np.log2(bpm / 75.0) / 0.35) ** 2))
        for phase in np.arange(0, period, max(1.0, period / 24)):
            idx = np.arange(phase, len(env) - 1, period).astype(int)
            if len(idx) < 8:
                continue
            sc = env[idx].mean() * prior
            if sc > best[0]:
                best = (sc, bpm, phase)
    return best[1], best[2]


def fit_grid(env, bpm0):
    """Refine a known tempo (from a tracklist tag): phase only, plus a narrow
    +/-3% window to absorb the difference between a stated round number and the
    actual recorded tempo."""
    fps = RATE / HOP
    best = (-1.0, bpm0, 0.0)
    for bpm in np.arange(bpm0 * 0.97, bpm0 * 1.03, 0.05):
        period = fps * 60.0 / bpm
        if period < 2:
            continue
        for phase in np.arange(0, period, max(1.0, period / 48)):
            idx = np.arange(phase, len(env) - 1, period).astype(int)
            if len(idx) < 8:
                continue
            sc = env[idx].mean()
            if sc > best[0]:
                best = (sc, bpm, phase)
    return best[1], best[2]


def pick_downbeat(env, bpm, phase):
    fps = RATE / HOP
    period = fps * 60.0 / bpm
    best, best_v = 0, -1.0
    for off in range(4):
        idx = np.arange(phase + off * period, len(env) - 1, period * 4).astype(int)
        if len(idx) < 2:
            continue
        v = env[idx].mean()
        if v > best_v:
            best_v, best = v, off
    return phase + best * period


def write_wav(path, s16):
    data = struct.pack("<%dh" % len(s16), *s16)
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        fh.write(struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        fh.write(b"data" + struct.pack("<I", len(data)) + data)


def slug(prefix, title):
    s = "".join(c.lower() for c in title if c.isalnum())[:16]
    return f"{prefix}{s}"[:19]


os.makedirs("out", exist_ok=True)
rows = []
print(f"{'name':<20}{'title':<22}{'bpm':>7}{'src':>5}{'bars':>6}{'sec':>7}{'MB':>6}")
for path, prefix, total, tracks in PACKS:
    for i, (start, title, hint) in enumerate(tracks):
        end = tracks[i + 1][0] if i + 1 < len(tracks) else total
        x = decode(path, start, end - start)
        if len(x) < RATE:
            print(f"  !! {title}: too short, skipped")
            continue
        env = onset_env(x)
        if hint:
            bpm, phase = fit_grid(env, float(hint))
        else:
            bpm, phase = search_grid(env)
        down = pick_downbeat(env, bpm, phase)

        s0 = int(down * HOP)
        bar = RATE * 60.0 / bpm * 4
        avail = (len(x) - s0) / bar
        bars = min(MAX_BARS, int(avail))
        if bars < MIN_BARS:
            bars = max(1, int(avail))
        n = int(round(bars * bar))
        if s0 + n > len(x):
            n = len(x) - s0
        seg = x[s0:s0 + n].copy()
        if len(seg) < RATE:
            print(f"  !! {title}: no usable bars, skipped")
            continue

        xf = int(RATE * XFADE_MS / 1000)
        if xf > 0 and s0 >= xf and len(seg) > 2 * xf:
            pre = x[s0 - xf:s0]
            ramp = np.linspace(0, 1, xf)
            seg[-xf:] = seg[-xf:] * (1 - ramp) + pre * ramp

        peak = float(np.max(np.abs(seg))) or 1e-9
        seg = seg * (TARGET_PEAK / peak)
        s16 = np.clip(np.round(seg * 32767), -32768, 32767).astype(np.int16).tolist()

        name = slug(prefix, title)
        write_wav(f"out/{name}.wav", s16)
        rows.append((name, title[:23], round(bpm, 1), bars, len(s16)))
        # A result sitting on a search bound means the search saturated, not
        # that the tempo is really there - surface it rather than shipping it.
        flag = "" if hint else (" <-- AT BOUND" if bpm <= 59.0 or bpm >= 104.0 else "")
        print(f"{name:<20}{title[:21]:<22}{bpm:>7.1f}"
              f"{('tag' if hint else 'est'):>5}{bars:>6}{len(s16)/RATE:>7.1f}"
              f"{len(s16)*2/1024/1024:>6.2f}{flag}")

with open("out/loops_vol2.txt", "w") as fh:
    for name, title, bpm, bars, frames in rows:
        fh.write(f"{name}\t{title}\t{bpm}\t{bars}\t{frames}\n")
tot = sum(r[4] for r in rows) * 2 / 1024 / 1024
print(f"\n{len(rows)} loops, {tot:.1f} MB total, largest "
      f"{max(r[4] for r in rows)*2/1024/1024:.2f} MB")
