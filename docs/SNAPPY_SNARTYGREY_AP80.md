# Snappy and SNARTYGREY on the AP80 Pro Max

This port packages native 360 x 640 versions of two themes from the official
Rockbox theme site:

- [Snappy 1.3](https://themes.rockbox.org/index.php?themeid=3935), by
  Christian Soffke. The original targets 240 x 320.
- [SnaryGrey](https://themes.rockbox.org/index.php?themeid=3998), by
  E. Halpin, based on Simon Anden's SNARTY. The installed theme name remains
  `SNARTYGREY`, matching the official package. The original targets 320 x 240.

Both themes are distributed under CC-BY-SA 4.0 by their authors. Their source
headers also credit the earlier SPAZZ, SNAZZ/SNAZZ2, SNAZZY, and Adwaitapod
work on which they build. The AP80 adaptations retain those headers in both
skin files.

The bundled Kode Mono Rockbox fonts are the unmodified `.fnt` files supplied
by the official theme packages. Kode Mono is Copyright 2023 The Kode Mono
Project Authors and is distributed under the SIL Open Font License 1.1; its
license is in `licenses/fonts/KodeMono-OFL.txt`.

## Porting rules

The AP80 skins preserve the original themes' typeface, one-bit artwork,
palette behavior, metadata order, inverse blocks, status indicators, peak
meters, progress bars, lock behavior, and now-playing footer behavior.
Nearest-neighbor AP80 variants of small one-bit sprites are included where
the original physical size would be too small at 249 DPI.

Snappy keeps its portrait layout and includes the five color examples from
the official package as directly selectable themes:

- Snappy Clear Skies
- Snappy Lemonade
- Snappy Neon Ivy
- Snappy Red-Eye Flight
- Snappy Wild Berry

SNARTYGREY retains its split album-art and metadata layout. Its original
menu-footer `Next Up` treatment is also used on the taller AP80 WPS, allowing
the portrait screen to carry more information without inventing a different
visual system.

## AP80 interaction map

Both now-playing screens use the same full-screen touch map:

| Region | Tap | Hold or repeated press |
| --- | --- | --- |
| Top left 120 x 80 | Return to browser | - |
| Top center 120 x 80 | Main menu | - |
| Top right 120 x 80 | Context menu | - |
| Main content | Playlist | Context menu |
| Progress band | Seek | - |
| Bottom left 120 x 80 | Previous track | Rewind |
| Bottom center 120 x 80 | Play/pause | Stop |
| Bottom right 120 x 80 | Next track | Fast-forward |

Menu skins reserve the full top-left 120 x 80 area for Back and the top-right
120 x 80 area for the context menu. Lists use 12-pixel side insets, the
29-pixel Kode Mono font, 36-pixel icon cells where icons are enabled, and
Rockbox's automatic touchscreen row padding. When playback is stopped, the
list reclaims the now-playing footer and exposes a sixth complete row.

The complete device sizing rationale is in
`docs/AP80_PRO_MAX_UI_SPEC.md`.
