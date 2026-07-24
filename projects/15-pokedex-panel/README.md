# Pokedex Panel

An offline creature field guide for the Elecrow CrowPanel Advanced 7-inch
display.

Browse or search a catalog on the left and get a large detail card on the right:
generated Poke Ball art, type chips, stats, buddy distance, and second-move dust,
plus extra pages for trainer notes, weaknesses, resistances, evolution, and
moves. It ships with a built-in catalog, so it is useful the moment it boots with
no SD card present.

This is a touch rebuild of a Cardputer app that ran on a keyboard and a small
screen; here it becomes a 1024x600 touch dashboard.

> This is Project 15 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready across the baseline, display, SD, and display-plus-SD builds.
Nothing has been uploaded or observed on a physical CrowPanel: the SD_MMC mount,
the touch coordinates, and full-card JSON browsing all still need captured
evidence. See the [technical reference](TECHNICAL.md).

## What you get

- A browse and search list covering names, dex numbers, types, and variants
- A large detail card with generated art and type chips
- Five detail pages per entry, paged by touch or by command
- Touch controls: tap a row to open it, `LIST` to go back, `PAGE -` / `PAGE +`
  to move through pages, `PREV` / `NEXT` to page the list
- An offline mock catalog compiled into the sketch
- Optional SD catalog streaming from the original project's card layout

## Not yet ported

Sprite BMP rendering and the original Cardputer audio. The display here is
Arduino_GFX only, matching the rest of this repo, and the SD path reads the
catalog index and detail JSON but not the sprites or audio files.

## Source boundary

This is a local, offline educational field guide built on generated data from the
source project. It does not connect to Pokémon GO, scrape accounts, or use any
game service, and it is not affiliated with or endorsed by the rights holders.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
