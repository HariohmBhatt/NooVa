# Nova UI local replica

This is a throwaway, dependency-free browser replica of the 320 × 480 LVGL
surface in firmware/ui/UiController.cpp. It is intentionally separate from
firmware production code and keeps all mock state in memory.

Start it from the repository root:

    ./scripts/ui-preview.sh

Then open http://127.0.0.1:4173. The local surface includes the Home, Server,
Setup, Diagnostics, Wi-Fi, SSH, and Logs pages. Use the mock scenario buttons
beside the device to inspect live, degraded, and setup-required hub states.
The Wi-Fi and SSH password flows include a touch-style on-screen keyboard.
