# AP80 Pro Max UI usability specification

This document defines reusable sizing and interaction constraints for themes
and application UI on the Hidizs AP80 Pro Max. It deliberately does not
specify colors, typeface families, illustration style, or visual branding.

The requirements are based on measurements from the official firmware's
LiteGUI layouts. They are intended to preserve the stock UI's successful
finger usability while using Rockbox's 480 x 640 framebuffer efficiently.

The words **must**, **should**, and **may** indicate requirement strength.

## Coordinate systems

The device exposes two relevant coordinate systems:

- Rockbox draws to a 480 x 640 framebuffer at 271 dpi.
- The stock UI and CST8xx touch controller use a 360 x 640 logical space.
- Rockbox scales raw touch X coordinates from 360 to 480. Y is unchanged.

Convert a stock horizontal measurement for a Rockbox layout with:

```
rockbox_x = round(stock_x * 4 / 3)
rockbox_y = stock_y
```

Theme geometry must be authored in Rockbox's 480 x 640 coordinates. Do not
place a 360-pixel-wide layout in the center of the framebuffer: that discards
25 percent of the available width. When porting an existing composition,
adapt its artwork to a 440-pixel content column and keep its touch layer on
the full 480-pixel canvas.

## Baseline metrics

These are the default metrics for new UI. A theme may be more spacious, but
must not go below the stated minimums.

| Element | Stock measurement | AP80 Rockbox requirement |
| --- | ---: | ---: |
| Passive status strip | 24 px high | 24 px high |
| Navigation header | 56 px high | 56 px high |
| Total upper chrome | 80 px high | 80 px high |
| Main/list text | 18-20 px | 18 px minimum; 21 px preferred |
| Secondary text | 12-16 px | 14 px preferred; 12 px minimum |
| Passive status text | 12 px glyphs | 14 px preferred; 12 px minimum |
| Dialog emphasis text | 20-28 px | 21-28 px |
| Standard list row | 80 px | 80 px minimum |
| Compact list row | 68 px | Only for single-line, secondary lists |
| Content side inset | 4-20 logical px | 20 px default; 12 px minimum |
| Standard action target | 60 x 56 logical px | 80 x 56 px |
| Edge/back target | 90 x 56 logical px | 120 x 56 px |
| Primary transport target | 70 x 70 logical px | At least 93 x 70 px |
| Two-way dialog action | 179 x 64 logical px | About 239 x 64 px |

The smallest recurring stock header action is 49 x 56 logical pixels, or
about 65 x 56 framebuffer pixels. Therefore no independent touch target may
be smaller than 64 x 56 pixels. Use 80 x 56 or larger whenever the layout
permits it.

Font sizes refer to the rendered pixel height, not a font file's name or
nominal point size. A theme author must inspect the compiled font rather than
assuming that two fonts with the same nominal size occupy the same height.

## Top bar and navigation

The upper 80 pixels should be treated as two functional bands:

1. A passive status band up to 24 pixels high for time, battery, playback,
   connectivity, or storage state.
2. A 56-pixel navigation band for the screen title, back action, and at most
   one or two contextual actions.

A theme may visually combine the bands, but it must preserve their total
80-pixel interaction area.

- A navigation title must render at least 18 pixels high; 21 pixels is the
  preferred Rockbox size.
- Passive status values must render at least 12 pixels high. Use 14 pixels
  when space permits.
- Battery state should include a numeric percentage when a small icon alone
  cannot communicate the level reliably.
- A title must receive the widest uninterrupted region. Status items must not
  reduce an ordinary title to less than half of the screen width.
- The top-left back target must be at least 120 x 56 pixels. Extending it to
  120 x 80 is recommended.
- A top-right contextual target should be at least 80 x 56 pixels. Extending
  it to 120 x 80 keeps the header symmetric and easier to learn.
- Passive status indicators must never create dead gaps inside a declared
  back or context target; the target may sit invisibly behind them.

Seven-pixel text is not acceptable for titles, clocks, battery values, menu
labels, metadata, or any other information needed during normal operation.

## Lists and menus

The stock player dedicates the area from Y=80 to Y=640 to content and normally
divides it into seven 80-pixel rows.

- A normal row must be at least 80 pixels high and its entire available width
  must be selectable.
- DPI-aware padding may make rows 88-96 pixels high when this improves touch
  accuracy. Ordinary screens should still expose at least five complete rows;
  six or seven are preferred for browsing.
- A one-line row should vertically center its label.
- A two-line row should use 18-21 pixel primary text and at least 12-14 pixel
  secondary text, with no overlap between their line boxes.
- Selection feedback must cover the full row, not just the text or icon.
- The default content viewport should use 20-pixel side margins, leaving
  440 pixels of usable width.
- Do not use more than 32 pixels of side inset for a standard list unless a
  persistent control occupies that space.
- Reserve 12-16 pixels between an icon and its label. Use at least 8 pixels
  between unrelated visual elements.
- With the preferred 21-pixel OneBit list font, use 48 x 32 icon cells. The
  glyph should occupy most of the cell height without touching its bounds;
  a 24-pixel-tall cell looks undersized beside this font.
- Long labels should receive remaining width before decorative elements do.
  Truncate or scroll only after the usable width has been exhausted.

## Buttons, icons, and touch targets

The visible symbol and the touch target are separate measurements. In the
stock Now Playing screen, 28-35 pixel transport glyphs sit inside 70 x 70
targets, commonly with about 20 logical pixels of invisible reach around the
glyph.

- A visible icon may be 24-36 pixels high, but its target must satisfy the
  target sizes above. In lists, aim for an icon height comparable to the
  rendered capital height plus ascenders rather than the font's nominal size.
- Primary playback controls must use targets at least 93 x 70 pixels.
- The first and last controls in a row should extend their hit areas to the
  screen edges; do not leave narrow untappable edge strips.
- Adjacent targets may share a boundary, but may not overlap with different
  actions.
- Long-press actions must use the same target as their short-press action.
- A control must show press or focus feedback immediately. The visual state
  and the action that will fire must always agree.
- Disabled controls must remain non-actionable even if their old touch region
  is still geometrically present.

For continuous controls such as seek and volume, the interactive band should
be at least 56 pixels high. A thin visual line is acceptable only when it sits
inside that larger invisible target.

## Spacing and information density

- Use 20-pixel outer side margins for text-heavy screens.
- Use 12 pixels as the minimum separation between a screen edge and readable
  text, except where a full-bleed selection background intentionally reaches
  the edge.
- Maintain at least 8 pixels of clear space around text line boxes.
- Keep related label/value pairs within one row or one 56-pixel control band.
- Avoid more than two text sizes in a list row and more than three text sizes
  on a normal screen.
- Status information must not compete with the screen's primary task. Move
  optional detail into a context screen before shrinking primary text.
- Modal choices should use the full width. A two-choice confirmation should
  split the bottom into two targets of roughly 240 x 64 pixels each.

## Navigation and hardware controls

Every touch screen must remain operable with the AP80's physical controls.

- Focus order must follow visual order.
- One volume-wheel detent should move one list item or one predictable value
  step.
- Previous/next and back/forward meanings must match the enclosure legends.
- The play button should activate the focused item where Rockbox conventions
  require selection.
- The power button must not be the only way to leave a screen.
- A consistent header back target is required for touch-only navigation.
- Returning from a child screen should restore the previous list position and
  focus whenever Rockbox exposes that state.

## Interaction feedback and timing

- Touch-down or focus feedback should appear within one display update and no
  later than 100 ms.
- A tap should not trigger the row crossed during an intentional vertical
  scroll.
- Long-press feedback should begin before the long-press action fires.
- Loading indicators must not displace the title or change touch geometry.
- Dynamic status values should use fixed-width viewports so clock or battery
  changes do not move adjacent controls.

## Validation checklist

Before accepting a new theme or screen:

1. Render it at exactly 480 x 640 with the simulator configured for 271 dpi.
2. Verify no required text is below the minimum rendered size.
3. Overlay every touch rectangle and check the 64 x 56 absolute minimum.
4. Verify normal rows are at least 80 pixels high and full-width selectable.
5. Test the longest available translated labels, 12/24-hour clocks, and
   battery values from 0% through 100%.
6. Test tap, long press, vertical scroll, and edge touches.
7. Navigate the complete screen using only physical buttons and the wheel.
8. Confirm touch and hardware focus select the same item.
9. Capture at least a menu/list screen, Now Playing, a dialog, and a
   text-heavy/plugin screen.
10. Run the skin parser and the AP80 release verifier.

## Stock firmware evidence

The measurements above were recovered from these files in the official
firmware root filesystem:

- `usr/resource/layout/theme1/topbar/topbar.view`: 360 x 24 status strip,
  12-pixel numeric glyphs.
- `usr/resource/layout/theme1/hiby_sub_back.view`: 360 x 56 navigation
  header, 18-pixel title, 90 x 56 back target, and 49-60 x 56 actions.
- `usr/resource/layout/theme1/listview/vg_listview_music.listview`: eight
  partitions over 640 pixels, 18-pixel primary text, and 12-pixel metadata.
- `usr/resource/layout/theme1/hiby_main_category.view`: 20-pixel labels and
  174 x 174 category targets.
- `usr/resource/layout/theme1/hiby_playing_plane.view`: 16-18 pixel playback
  text, 70 x 70 transport targets, and a 70-pixel seek band.
- `usr/resource/layout/theme1/dialog/confirm.dlg`: 20-28 pixel message text
  and two 179 x 64 bottom actions.

These files are measurement references only. New UI must follow the
constraints above rather than copy their colors, fonts, or artwork.
