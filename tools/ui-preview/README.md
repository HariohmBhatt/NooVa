# Nova UI local preview

This is a dependency-free browser concept preview for the 320 × 480 Nova
terminal. It is intentionally separate from firmware production code and
keeps all mock state in memory.

Start it from the repository root:

    ./scripts/ui-preview.sh

Then open http://127.0.0.1:4173. The product surface is organized around
Home, Rooms, Assist, Hub, and More. More contains the Wi-Fi, SSH, Diagnostics,
and Logs child flows. Use the mock scenario controls beside the device to
inspect live, degraded, offline, and setup-required hub states.

Rooms, scenes, and assistant responses are deterministic browser fixtures. The
current terminal protocol does not yet define Home Assistant control or voice
session messages, so the preview labels those interactions as mock behavior
and never represents them as real server execution. The Wi-Fi and SSH flows
retain touch-style password keyboards and clear credentials after each flow.

The page exposes a small `window.novaPreview` helper for smoke tests with
`state`, `chooseScenario()`, `showPage()`, and `render()`. It is a browser-only
preview API; it is not part of the firmware protocol.

The preceding `fixtures.js` script provides the internal `window.NovaFixtures`
namespace consumed by the app bundle. It keeps fixture data and preview timing
constants separate from rendering and interaction logic.
