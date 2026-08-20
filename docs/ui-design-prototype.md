# NOVA Sentinel 0.1 UI prototype

> Prototype only. This explores the question **“which information hierarchy
> makes a 320 x 480 always-on server sentinel clearest and most attractive?”**
> It is not production firmware and should be rewritten in LVGL after review.

## Run it

```sh
python3 tools/ui-preview-prototype/serve.py
```

Open <http://127.0.0.1:4173/?variant=D>. `?variant=A`, `B`, `C`, or `D` directly
selects a design. The floating bar switches variants, every required fixture,
and Home/Details. Left/right keys switch variants and up/down keys switch
fixtures when a form control is not focused. The exact display canvas is 320 x
480 CSS pixels; the outer gray browser stage and white switcher are review
tools, not part of the proposed device UI.

With the preview server running, repeat the production-candidate symbol audit:

```sh
python3 tools/ui-preview-prototype/audit_d.py
```

## Shared product decisions

- State and primary reason occupy the strongest visual area. Freshness is
  always in the header and changes from `LIVE` to the explicit `LAST KNOWN`.
- Icons, labels, and shapes accompany every color. Red, amber, blue, purple,
  and gray communicate progressively different states without being the only
  signal.
- CPU, memory, and disk are secondary. Null metrics use `Unavailable` or an
  em dash rather than `0`.
- Home has one 44-pixel `Details` target; Details has one 44-pixel `Back`
  target. The prototype adds no device action beyond the 0.1 contract.
- Layout is static. Only labels, simple fills, and once-per-second age text
  need change, which limits LVGL invalidation and display traffic.

## Variant A — Beacon

A calm, centered status beacon consumes the top 40% of the content, followed
by readable horizontal metric bars and a compact 2 x 2 service grid. It is the
best across-the-room answer to “is everything okay?” and gives warning text
enough width for the longest bounded summaries.

Tradeoff: it reveals the failed network layer through wording rather than a
structural diagram.

## Variant B — Instrument

A full-height severity rail makes state unmistakable while the remaining
surface becomes a compact cockpit: numeric metric cells, a two-by-two check
matrix, and terse monospaced labels. It carries the most information without
scrolling and feels like dedicated hardware.

Tradeoff: usable width drops from 320 to 272 pixels. Long explanations and
service names need aggressive clipping, and the density is less calm for an
always-visible household display.

## Variant C — Signal path

The information architecture follows the actual dependency chain: Wi-Fi,
server, then monitor. Offline fixtures mark exactly where the chain failed and
dim layers that could not be evaluated. Metrics and service indicators become
a compact footer.

Tradeoff: it is the clearest diagnostic view but gives healthy screens more
structural prominence than they need. It also uses valuable vertical space on
three normally healthy rows.

## Recommendation for review

Use **Variant A as the Home foundation**, but borrow the three-layer connection
path from **Variant C for Details and all offline/error Home states**. This
hybrid keeps healthy operation calm and readable at a distance, while making
Wi-Fi/server/monitor failures unusually easy to distinguish. Borrow Variant
B’s compact numeric treatment only if hardware inspection shows the horizontal
bars produce excessive redraw cost; do not retain its permanent rail because
the lost text width hurts bounded strings.

The independent review should focus on arm’s-length typography, the usefulness
of service abbreviations, whether last-known data needs a stronger full-width
surface treatment, and how much of the Signal Path should appear on Home.

## Variant D — Sentinel hybrid (production candidate)

This implements the approved hybrid without deleting the exploratory sources.
Fresh states use Beacon’s calm hero, neutral metric bars, and a 2 x 2
glyph-plus-name service grid. Connectivity/freshness failures replace the bars
with Signal Path’s diagnostic chain. Retained snapshots receive a full-width
amber `LAST KNOWN DATA` band, dimmed metrics/services, and an explicit
`LAST KNOWN` service label. Details is scrollable beneath a fixed Back button.

The production choices are rationalized in `docs/ui-design.md`.

## Visual validation performed

The collaborative T3 preview was opened first, but its navigation client could
not load either the environment-port target or localhost after reopen and
retry. A local headless Chromium fallback then inspected the rendered artifact.
The initial pass visited all 60 combinations of 3 variants, 10 fixtures, and 2 pages. Every
device root measured exactly 320 x 480, each screen exposed one 44-pixel touch
target, no root had horizontal or vertical overflow, and the browser reported
no JavaScript or console errors.

Native-size screenshots were inspected for A Healthy/Home, A Longest/Home, A
Null metrics/Home, A Critical/Details, B Critical/Home, B Setup/Home, B Longest
strings/Details, C Wi-Fi offline/Home, C Server offline/Home, and C Monitor
error/Details. That pass prompted explicit glyphs in every compact service
indicator and full service-state words on B’s Details screen.

### Refined validation

After independent review, a second pass visited all 88 combinations of 4
variants, 11 precedence-valid fixtures, and 2 pages. All remained exactly 320 x
480 with one 44 px navigation target and no root overflow or browser errors.
For the maximum fresh fixture, each variant’s Details container had at least 56
px bottom padding; after scrolling to its endpoint, the third reason and fourth
full service state were above and clear of the fixed Back button.

Native-size Variant D screenshots were inspected for max fresh Home, max
offline Home, stale Home, monitor error Home, setup Home, and both the top and
bottom scroll positions of max fresh and max offline Details. The max-offline
fixture deliberately presents `Server offline` as current device state while
the previous critical severity, reasons, metrics, and services remain visibly
labeled and dimmed as retained data.

### Final Variant D accessibility pass

A focused pass over all 22 Variant D fixture/page combinations found no visible
text below 10 px; freshness and Home service names are 11 px. Operational
retained text remains fully opaque, while darker panels and borders carry the
visual de-emphasis. A computed-color audit found no normal text below 4.5:1;
the minimum observed ratio was 5.25:1. The tiny path indices were removed, the
scroll cue is 10 px, navigation uses ASCII `< Back` / `Details >`, and stale is
a drawn arc-and-arrowhead mark rather than a Unicode glyph. Maximum fresh and
maximum retained Details still expose all three reasons and four service
states above the fixed Back target at maximum scroll.
