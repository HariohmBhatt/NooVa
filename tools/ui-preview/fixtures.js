(() => {
  const fixtures = Object.freeze({
    stats: Object.freeze({
      disconnected: Object.freeze({
        wifi: "Not connected",
        ipAddress: "—",
        uptime: "00:00:00",
        memoryFree: "6.8 MB",
        temperature: "32.4 °C",
        firmware: "v0.1.0",
      }),
      connected: Object.freeze({
        ipAddress: "192.168.29.18",
        uptime: "02:14:08",
        memoryFree: "6.4 MB",
        temperature: "33.1 °C",
        firmware: "v0.1.0",
      }),
    }),
    networks: Object.freeze([
      Object.freeze({ ssid: "Nova-24G", signal: "Strong", secure: true }),
      Object.freeze({ ssid: "Home network", signal: "Good", secure: true }),
      Object.freeze({ ssid: "Studio guest", signal: "Fair", secure: false }),
    ]),
  });

  window.NovaFixtures = fixtures;
})();
