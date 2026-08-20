# NOVA Sentinel 0.1 production UI handoff

## Chosen direction

Implement prototype **Variant D — Sentinel hybrid**. It uses Beacon’s calm,
state-first Home for fresh snapshots and switches to Signal Path’s explicit
Wi-Fi → Server → Monitor hierarchy for stale, offline, monitor-error, and setup
states. Details is a fixed-header/fixed-Back scrolling surface.

The HTML prototype is a behavioral and visual reference, not LVGL source. Keep
the production UI driven only by `DeviceView`; do not duplicate freshness or
connection-state logic in widgets.

## 320 x 480 layout

Home uses these stable regions:

- 42 px status header.
- Optional 30 px full-width `LAST KNOWN DATA` band.
- 106–151 px state hero with shape, state text, and primary reason.
- Fresh states: neutral CPU/memory/disk bars. Diagnostic states: three-row
  connection path with only proven claims.
- A compact 2 x 2 service grid with glyph, ellipsized service name, and an
  explicit `LAST KNOWN` count when retained.
- Fixed 44 px Details button with at least 10 px edge clearance.

Details keeps the 42 px header, optional 30 px last-known band, and fixed 44 px
Back button. Everything between them scrolls. Add at least 56 px bottom padding
inside the scroll content so the last service row can clear the button.

## Typography

Use one bundled LVGL sans-serif family only. Four sizes are sufficient:

| Token | Size | Use |
| --- | ---: | --- |
| `display` | 27 px | Home state |
| `title` | 23 px | Details state |
| `body` | 12 px | Reasons, summaries, service names |
| `label` | 10 px | Metadata, metric labels, path status |

Use bold/regular weights from the same family. Do not add a monospace font;
tabular metric alignment can use fixed widget widths.
Freshness and Home service names use 11 px. No user-visible metadata may render
below 10 px.

## Precomputed palette

Use compile-time LVGL colors; do not calculate blends or opacity-derived theme
colors during rendering.

| Token | RGB hex |
| --- | --- |
| Background | `#0B1115` |
| Panel | `#121B21` |
| Raised panel | `#172229` |
| Divider | `#27343D` |
| Primary text | `#F3F6F7` |
| Secondary text | `#91A0AA` |
| Healthy | `#66DFAE` |
| Warning / last-known band | `#FFC857` |
| Critical / offline | `#FF6B63` |
| Stale | `#7FC8FF` |
| Monitor error | `#D7A6FF` |
| Neutral metric fill | `#79A9B3` |

Retained operational text stays fully opaque. Distinguish retained regions with
the full-width warning band and darker `#0D1418` panels / `#1B272E` borders;
do not reduce text opacity. Normal-sized text must retain at least 4.5:1
contrast against its actual background.

## LVGL-safe symbols

Variant D requires only ASCII font glyphs: `OK` healthy, `!` warning/error,
`X` critical/offline, `-` unknown/not evaluated, and `+` setup. Navigation is
`< Back` and `Details >`.

The stale mark is not a font glyph. Draw it from a 270-degree circular arc plus
a small triangular arrowhead using LVGL primitives. It must have the accessible
text label `Data stale`. Never substitute the Unicode rotating-arrow glyph,
emoji, or a platform-dependent icon font.

Every state combines symbol/shape and text with color. Compact services use a
colored 15 px glyph square plus an ellipsized name; color is never the only
state signal.

## State-specific wording

- Fresh snapshots may show `Connected`, `Reachable`, and `Validated` because
  the current accepted poll proves them.
- Stale shows Wi-Fi `Connected`, Server `Unknown`, Monitor `Not evaluated`.
- Server offline shows Wi-Fi `Connected`, Server `No response`, Monitor `Not
  evaluated`.
- Wi-Fi offline shows Wi-Fi `Disconnected`; downstream layers are `Not
  evaluated`.
- Monitor/contract/TLS errors do not imply current server health: show Server
  `Unknown` and Monitor `Not evaluated`, with the concrete error as the primary
  reason.
- Setup uses neutral color and `Not configured` for all three layers.

## Rendering constraints

Create widgets once and update labels, colors, bar values, and visibility only
when `DeviceView` changes. Update visible age/uptime text once per second. Do
not animate, redraw the full screen on a timer, compute gradients, or allocate
strings during ordinary refresh. Percent bars use the neutral metric fill so
overall severity changes do not repaint them as alarm surfaces.

Longest protocol strings must wrap in Details rather than shrink below 12 px.
Home may clamp the primary reason to four lines. Service names ellipsize on
Home and appear in full in scrollable Details.
