(() => {
  const fixtures = Object.freeze({
    clock: "19:17",
    connected: {
      network: "Nova-24G",
      ipAddress: "192.168.29.18",
      statusCopy: "NOVA is online and ready on your local network.",
    },
    disconnected: {
      statusCopy: "Connect NOVA to Wi-Fi to bring this device online.",
    },
    stats: Object.freeze({
      uptime: "00:00:00",
      memoryFree: "6.8 MB",
      temperature: "32.4 °C",
      firmware: "0.1.0",
    }),
    networks: Object.freeze([
      Object.freeze({ ssid: "Nova-24G", signal: "Strong", secure: true }),
      Object.freeze({ ssid: "Home network", signal: "Good", secure: true }),
      Object.freeze({ ssid: "Studio guest", signal: "Fair", secure: false }),
    ]),
  });

  window.NovaFixtures = fixtures;
})();
