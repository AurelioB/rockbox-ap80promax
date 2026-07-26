# OneBit themes for the Hidizs AP80 Pro Max

The AP80 Pro Max variants are ports of the original Rockbox themes, not
redraws. Their compiled fonts and source bitmap resources are preserved from
the published theme packages. AP80-specific bitmap variants use
nearest-neighbour scaling where larger touch-oriented list cells need it,
without smoothing or reinterpretation. Skin coordinates, viewport sizes, and
invisible touchscreen regions are adapted for the device.

## Sources

| Theme | Rockbox theme ID | Original authors |
| --- | ---: | --- |
| OneBit_OLED | 3901 | Jihoon Kim; based on OneBit_VFD by chronicallyoffline |
| OneBit_VFD2 | 3845 | chronicallyoffline, Cicumpunct |
| OneBit_VFD_ALT | 3828 | chronicallyoffline, Cicumpunct, Its_Just_Luck |
| OneBitMono | 3824 | chronicallyoffline, Cicumpunct |
| OneBitMono2 | 3819 | chronicallyoffline, Cicumpunct |
| OneBit_Orange | 3633 preset | chronicallyoffline |
| OneBit_Green | 3633 preset | chronicallyoffline |

The theme headers declare CC BY-SA licensing and credit Chuck Lardo and D0-0K
for reused CC BY-SA and GPLv3 material. The bundled LanaPixel font is credited
to eishiya in the original OneBit_OLED package.

## AP80 adaptation

- Visible logical display: 360 x 640, 16-bit color.
- The original 360 x 640 composition fills the physical display directly.
- AP80 meter, progress, volume, album-art fallback, and icon-sheet variants
  are nearest-neighbour derivatives of the original theme assets. The source
  assets and fonts remain available unchanged.
- Original font files are packaged without conversion.
- Original WPS/SBS image-backed animations, peak meters, progress bars,
  volume bars, conditional settings, USB screen, and hold screen are retained.
- AP80 list icon sheets retain the original glyphs in 48 x 32 cells so their
  visible height balances the 21-pixel LanaPixel list text.
- Album art retains the source theme's fallback graphic. Its AP80 viewport is
  enlarged only where needed to balance the wider composition.
- `OneBit_Orange` and `OneBit_Green` use the exact foreground/background
  values from the original package. Their skins, list icons, and WPS bitmap
  palettes are deterministic color variants of the adapted `OneBitMono`
  files: dimensions, bitmap data, frame counts, layout, and touch behavior
  remain identical.
- A visual-neutral, full-screen touch layer makes the themes practical on the
  small display without changing their appearance. The header and transport
  rows divide the full screen into three 120 x 80 targets. Volume and seek
  accept the full composition width. Holding the left, centre, or right
  transport zone rewinds, stops, or fast-forwards respectively.
- Tapping the WPS header's left, centre, or right zone opens the browser, main
  menu, or context menu. In lists, the enlarged left header zone is a
  dependable back/cancel target and the right zone opens the context menu.
- Applying any AP80 OneBit preset selects automatic DPI-aware list padding.
  At 249 dpi this gives touchable rows close to the stock firmware's
  68-to-80-pixel list items.
- Menus use readable text and icons sized to remain balanced within the
  360-pixel-wide content area.
- AP80 menu and playback headers use the full framebuffer width. Navigation
  text uses the 21-pixel UI font, compact status values use the bundled
  14-pixel font, and battery level is available numerically as well as
  graphically.

General sizing and interaction requirements for AP80 themes are documented
in [AP80_PRO_MAX_UI_SPEC.md](AP80_PRO_MAX_UI_SPEC.md).
