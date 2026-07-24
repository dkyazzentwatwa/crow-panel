# Pip-Boy SD Demo Pack

Copy this directory's **contents** to the root of a FAT32-formatted CrowPanel
microSD card. The terminal works without a card, but marks the state as
`SD DEMO` and uses its built-in map grid, item illustrations, and generated
speaker test instead of external assets.

```text
/pipboy/
├── audio/
│   ├── 01-atom-radio.wav
│   └── 02-outpost-log.wav
├── images/
│   ├── wasteland-map.bmp
│   └── outpost-card.bmp
└── logs/
    └── signal-log.txt
```

## Accepted media

- Audio: PCM WAV, 16-bit, mono, 16 kHz. The Radio page lists the first eight
  `.wav` files in `/pipboy/audio/` and loops no audio after a track ends.
- Images: uncompressed 24-bit BMP. Keep them at or below 590 x 370 pixels for
  the archive view, and at or below 676 x 402 pixels for `wasteland-map.bmp`.
- Text logs are a ready-to-use SD convention for future DATA expansion. V1
  displays the supplied built-in transmission copy rather than parsing logs.

Use only original or personally licensed media. This repository does not ship
Fallout audio, artwork, maps, game text, or other copyrighted game assets.
