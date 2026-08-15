(() => {
  const fixtures = Object.freeze({
    stats: Object.freeze({
      disconnected: Object.freeze({
        wifi: "Not connected",
        ipAddress: "—",
        cpu: "52%",
        gpu: "38%",
        uptime: "00:00:00",
        memoryFree: "6.8 MB",
        temperature: "32.4 °C",
        firmware: "v0.1.0",
      }),
      connected: Object.freeze({
        ipAddress: "192.168.29.18",
        cpu: "52%",
        gpu: "38%",
        uptime: "02:14:08",
        memoryFree: "6.4 MB",
        temperature: "33.1 °C",
        firmware: "v0.1.0",
      }),
    }),
    telemetry: Object.freeze({
      cpu: Object.freeze([28, 44, 36, 61, 48, 72, 52]),
      gpu: Object.freeze([12, 26, 18, 39, 31, 55, 38]),
    }),
    networks: Object.freeze([
      Object.freeze({ ssid: "Nova-24G", signal: "Strong", secure: true }),
      Object.freeze({ ssid: "Home network", signal: "Good", secure: true }),
      Object.freeze({ ssid: "Studio guest", signal: "Fair", secure: false }),
    ]),
  });

  window.NovaFixtures = fixtures;
})();
