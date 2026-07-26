#!/usr/bin/env python3
"""Regroup the flat loop pile into one folder per sample pack.

Flat /mpc/loops/*.wav with a single manifest was fine at 11 loops and is
unusable at 41. Each pack becomes its own directory with its own manifest, so
the panel can browse pack-then-loop instead of stepping through everything.

Names lose their pack prefix on the way in (sasoulaffair -> soulaffair): the
prefix only existed to keep a flat namespace unique, and the folder now does
that job.
"""
import os, shutil

ROOT = "/Users/cypher/Downloads/cypher-mpc-kit-SDREADY/mpc/loops"

# pack dir -> (display title, [loop names as currently on disk])
PACKS = {
    "revival": ("Revival", [
        "burdens", "cryout", "forevermore", "hisplan", "mercy", "quiettime",
        "sacrifice", "miracles", "thankful", "unconditional", "yourglory"]),
    "soulaffair": ("Soul Affair V1", [
        "sasoulaffair", "saholdinon", "sasinceyoubeengone", "saletmedown",
        "sakeepthefaith"]),
    "unreleased": ("Soul & Gospel 100", [
        "unlovewillkeepus", "unrzaslayer", "unwhileyouweregone",
        "unpackpreview1", "unpackpreview2"]),
    "cinema": ("Cinema Themes V1", [
        "cthighoffyou", "ctfugadallisola", "ctnotturno", "ctlemilieucriminal",
        "ctsergiosescape", "ctsaintthesinner"]),
    "temptations": ("Temptations V3", [
        "tmwanteditall", "tmbabyyourlove", "tmmarianasrevenge",
        "tmallthistime"]),
    "soulfulsonics": ("Soulful Sonics V6", [
        "sswhenyouneedme", "ssriseagain", "sssecrets", "ssjourneyahead",
        "ssreflections", "ssmysunshine", "sshardtruths", "sscosmiclove",
        "sstheslowburn", "ssloveandwar"]),
}

# The 2-char prefixes existed only for flat-namespace uniqueness.
STRIP = {"soulaffair": "sa", "unreleased": "un", "cinema": "ct",
         "temptations": "tm", "soulfulsonics": "ss"}

# Existing flat manifest: name -> (title, bpm, bars, frames)
meta = {}
with open(os.path.join(ROOT, "loops.txt")) as fh:
    for line in fh:
        parts = line.rstrip("\n").split("\t")
        if len(parts) == 5:
            meta[parts[0]] = parts[1:]

moved = 0
for pack, (title, names) in PACKS.items():
    d = os.path.join(ROOT, pack)
    os.makedirs(d, exist_ok=True)
    rows = []
    for old in names:
        src = os.path.join(ROOT, old + ".wav")
        if not os.path.exists(src):
            print(f"  !! missing {old}.wav")
            continue
        new = old
        pre = STRIP.get(pack)
        if pre and new.startswith(pre):
            new = new[len(pre):]
        shutil.move(src, os.path.join(d, new + ".wav"))
        t, bpm, bars, frames = meta[old]
        rows.append((new, t, bpm, bars, frames))
        moved += 1
    with open(os.path.join(d, "loops.txt"), "w") as fh:
        for r in rows:
            fh.write("\t".join(r) + "\n")
    with open(os.path.join(d, "pack.txt"), "w") as fh:
        fh.write(title + "\n")
    print(f"{pack:<16}{title:<20}{len(rows):>3} loops")

os.remove(os.path.join(ROOT, "loops.txt"))
left = [f for f in os.listdir(ROOT) if f.endswith(".wav")]
print(f"\nmoved {moved}; stray wavs left at top level: {len(left)}")
for f in left:
    print("  stray:", f)
