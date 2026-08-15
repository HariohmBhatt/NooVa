# Nova device status preview

This is a dependency-free browser preview for the 320 × 480 Nova terminal.
It intentionally presents one focused product surface:

- current device status and connection state;
- a short list of device stats, including CPU and GPU utilisation;
- one compact CPU/GPU utilisation time series; and
- one Wi-Fi connection action, with a minimal network/password sheet.

There is no navigation, assistant, room control, diagnostics console, or
desktop control harness in this preview. Those are separate product surfaces
and do not compete with the device's first-run connection task.

Start it from the repository root:

    ./scripts/ui-preview.sh

Then open http://127.0.0.1:4173. The Wi-Fi data is deterministic in-memory
fixture data and does not connect to a real network.

The page exposes `window.novaPreview` for browser smoke tests. It provides
`state`, `openWifi()`, `selectNetwork(ssid)`, `connect()`, `reset()`, and
`render()`; this helper is preview-only and is not part of the firmware
protocol. The app also consumes the documented internal
`window.NovaFixtures` object for deterministic display data.
