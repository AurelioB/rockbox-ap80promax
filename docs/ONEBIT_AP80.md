# OneBit themes for the Hidizs AP80 Pro Max

The AP80 Pro Max variants are ports of the original Rockbox themes, not
redraws. Their compiled fonts and bitmap resources are copied byte-for-byte
from the published theme packages. Only skin coordinates, viewport sizes, and
invisible touchscreen regions differ.

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

- Physical framebuffer: 480 x 640, 16-bit color.
- The original 360 x 640 composition is centered with 60-pixel side margins.
  Viewports and touch regions are adapted, while the source bitmaps and fonts
  remain unchanged.
- Original font files are packaged without conversion.
- Original WPS/SBS image-backed animations, peak meters, progress bars,
  volume bars, conditional settings, USB screen, and hold screen are retained.
- Original UI icon sets are retained. OneBit_OLED uses its author-provided
  360p icon set.
- Album art retains the dimensions and fallback graphic of the source theme.
- `OneBit_Orange` and `OneBit_Green` use the exact foreground/background
  values from the original package and share the adapted `OneBitMono` skin,
  SBS, fonts, icons, and bitmaps rather than duplicating or recoloring them.
- A visual-neutral, full-screen touch layer makes the themes practical on the
  small display without changing their appearance. The three 120 x 80 header
  targets exactly match the stock firmware's 90 x 60 logical header geometry
  after its 360-to-480 panel scaling. The three 120 x 80 bottom transport
  zones are comparable to its scaled 70 x 70 controls. Volume and seek accept
  the full composition width. Holding the left, centre, or right transport
  zone rewinds, stops, or fast-forwards respectively.
- Tapping the WPS header's left, centre, or right zone opens the browser, main
  menu, or context menu. In lists, the enlarged left header zone is a
  dependable back/cancel target and the right zone opens the context menu.
- Applying any AP80 OneBit preset selects automatic DPI-aware list padding.
  At 271 dpi this gives roughly 90-pixel rows, close to the stock firmware's
  scaled 68-to-80-pixel logical list items and much easier to select reliably.
- On the AP80's 480-pixel framebuffer, menus use the bundled 21-pixel
  LanaPixel font and original 360p icon set. Their content viewport is
  440 pixels wide with only 20-pixel side margins; the original 360-pixel
  WPS composition and its bitmap artwork remain unchanged.
- AP80 menu and playback headers use the full framebuffer width. Navigation
  text uses the 21-pixel UI font, compact status values use the bundled
  14-pixel font, and battery level is available numerically as well as
  graphically.

General sizing and interaction requirements for AP80 themes are documented
in [AP80_PRO_MAX_UI_SPEC.md](AP80_PRO_MAX_UI_SPEC.md).
