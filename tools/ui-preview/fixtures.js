(function () {
  "use strict";

  const PREVIEW_CONFIG = Object.freeze({
    maxLogEntries: 14,
    minPasswordLength: 4,
    toastDurationMs: 2200,
    wifiTransitionMs: 700,
    sshTransitionMs: 550,
    sshEnableMs: 500,
    deviceActionMs: 550,
    assistantThinkMs: 800,
    assistantResponseMs: 2300,
    clockRefreshMs: 30000,
    secondsPerDay: 86400,
    staleAfterSeconds: 15
  });

  const keyboardRows = [
    ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
    ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
    ["shift", "z", "x", "c", "v", "b", "n", "m", "backspace"],
    ["123", "space"]
  ];

  const initialRooms = [
    {
      id: "living",
      name: "Living room",
      icon: "light",
      temperature: "21°",
      summary: "2 lights on · evening scene",
      devices: [
        { id: "living-floor", name: "Floor lamp", detail: "Warm white · 60%", icon: "light", on: true },
        { id: "living-ceiling", name: "Ceiling lights", detail: "Warm white · 35%", icon: "light", on: true },
        { id: "living-air", name: "Air purifier", detail: "Quiet mode", icon: "spark", on: false }
      ]
    },
    {
      id: "kitchen",
      name: "Kitchen",
      icon: "spark",
      temperature: "20°",
      summary: "All quiet · 2 devices",
      devices: [
        { id: "kitchen-light", name: "Pendant lights", detail: "Off · ready", icon: "light", on: false },
        { id: "kitchen-plug", name: "Coffee machine", detail: "Standby", icon: "grid", on: false }
      ]
    },
    {
      id: "office",
      name: "Office",
      icon: "spark",
      temperature: "22°",
      summary: "Desk lamp on · focused",
      devices: [
        { id: "office-desk", name: "Desk lamp", detail: "Cool white · 80%", icon: "light", on: true },
        { id: "office-plug", name: "Monitor power", detail: "On · 42 W", icon: "server", on: true }
      ]
    },
    {
      id: "bedroom",
      name: "Bedroom",
      icon: "shield",
      temperature: "19°",
      summary: "Quiet · all devices off",
      devices: [
        { id: "bedroom-lamp", name: "Bedside lamp", detail: "Off · ready", icon: "light", on: false },
        { id: "bedroom-sensor", name: "Sleep sensor", detail: "Monitoring", icon: "activity", on: true }
      ]
    }
  ];

  const scenarioFixtures = {
    live: {
      hub: {
        valid: true,
        status: "Live",
        grade: "Normal",
        controlAvailable: true,
        latencyMs: 24,
        cpuPercent: 12.4,
        memoryPercent: 37,
        snapshotAgeSeconds: 2,
        error: "",
        alert: "No alerts · everything is settled",
        services: { hubApi: "healthy", metrics: "healthy", homeControl: "planned" }
      },
      wifi: { status: "Connected", ssid: "Nova-24G", ip: "192.168.29.18", rssi: -47 },
      toast: "Live hub scenario"
    },
    degraded: {
      hub: {
        valid: true,
        status: "Degraded",
        grade: "Warning",
        controlAvailable: true,
        latencyMs: 186,
        cpuPercent: 78.6,
        memoryPercent: 64,
        snapshotAgeSeconds: 9,
        error: "Metrics delayed · collector response is slow",
        alert: "CPU is running warm · review server health",
        services: { hubApi: "healthy", metrics: "slow", homeControl: "planned" }
      },
      wifi: { status: "Connected", ssid: "Nova-24G", ip: "192.168.29.18", rssi: -47 },
      toast: "Warning state loaded"
    },
    offline: {
      hub: {
        valid: true,
        status: "Offline",
        grade: "Unknown",
        controlAvailable: false,
        snapshotAgeSeconds: 18,
        error: "No current snapshot · controls are paused",
        alert: "Hub connection lost",
        services: { hubApi: "offline", metrics: "stale", homeControl: "paused" }
      },
      wifi: { status: "Connected", ssid: "Nova-24G", ip: "192.168.29.18", rssi: -47 },
      toast: "Controls paused while offline"
    },
    setup: {
      hub: {
        valid: false,
        status: "Setup required",
        grade: "Unknown",
        controlAvailable: false,
        snapshotAgeSeconds: null,
        error: "",
        alert: "Waiting for Wi-Fi and automatic registration",
        services: { hubApi: "waiting", metrics: "waiting", homeControl: "blocked" }
      },
      wifi: { status: "Not connected", ssid: "", ip: "0.0.0.0", rssi: 0 },
      toast: "Setup-required state loaded"
    }
  };

  window.NovaFixtures = Object.freeze({
    PREVIEW_CONFIG: PREVIEW_CONFIG,
    keyboardRows: keyboardRows,
    initialRooms: initialRooms,
    scenarioFixtures: scenarioFixtures
  });
})();
