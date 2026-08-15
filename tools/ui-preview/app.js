(() => {
  "use strict";

  const CHART_WIDTH = 288;
  const CHART_TOP = 8;
  const CHART_HEIGHT = 48;
  const UTILISATION_MAX = 100;
  const fixtures = window.NovaFixtures;
  const elements = {};
  const state = {
    connected: false,
    selectedNetwork: "",
    connectedNetwork: "",
    sheetOpen: false,
    passwordStep: false,
  };

  function cacheElements() {
    const ids = [
      "wifi-value",
      "ip-value",
      "cpu-value",
      "gpu-value",
      "uptime-value",
      "memory-value",
      "temperature-value",
      "firmware-value",
      "utilisation-chart",
      "cpu-line",
      "gpu-line",
      "cpu-current",
      "gpu-current",
      "wifi-action",
      "wifi-sheet",
      "wifi-close",
      "network-step",
      "network-list",
      "password-step",
      "wifi-back",
      "selected-network",
      "wifi-password",
    ];

    ids.forEach((id) => {
      elements[id] = document.getElementById(id);
    });
  }

  function renderStats() {
    const stats = state.connected ? fixtures.stats.connected : fixtures.stats.disconnected;
    elements["wifi-value"].textContent = state.connected ? state.connectedNetwork : stats.wifi;
    elements["ip-value"].textContent = stats.ipAddress;
    elements["cpu-value"].textContent = stats.cpu;
    elements["gpu-value"].textContent = stats.gpu;
    elements["uptime-value"].textContent = stats.uptime;
    elements["memory-value"].textContent = stats.memoryFree;
    elements["temperature-value"].textContent = stats.temperature;
    elements["firmware-value"].textContent = stats.firmware;
  }

  function chartPoints(values) {
    const step = CHART_WIDTH / (values.length - 1);
    return values
      .map((value, index) => {
        const x = index * step;
        const y = CHART_TOP + ((UTILISATION_MAX - value) / UTILISATION_MAX) * CHART_HEIGHT;
        return `${x.toFixed(1)},${y.toFixed(1)}`;
      })
      .join(" ");
  }

  function renderTelemetry() {
    const stats = state.connected ? fixtures.stats.connected : fixtures.stats.disconnected;
    elements["cpu-line"].setAttribute("points", chartPoints(fixtures.telemetry.cpu));
    elements["gpu-line"].setAttribute("points", chartPoints(fixtures.telemetry.gpu));
    elements["cpu-current"].textContent = stats.cpu;
    elements["gpu-current"].textContent = stats.gpu;
    elements["utilisation-chart"].setAttribute(
      "aria-label",
      `CPU utilisation ${stats.cpu}, GPU utilisation ${stats.gpu}, over the last ten minutes`,
    );
  }

  function renderNetworks() {
    elements["network-list"].replaceChildren();
    fixtures.networks.forEach((network) => {
      const button = document.createElement("button");
      button.className = "network-option";
      button.type = "button";
      button.dataset.ssid = network.ssid;
      button.innerHTML = `<span><strong>${network.ssid}</strong><small>${network.signal}${network.secure ? " · Password required" : " · Open network"}</small></span><span class="network-arrow" aria-hidden="true">→</span>`;
      button.addEventListener("click", () => selectNetwork(network.ssid));
      elements["network-list"].append(button);
    });
  }

  function renderSheet() {
    const open = state.sheetOpen;
    elements["wifi-sheet"].hidden = !open;
    elements["wifi-sheet"].setAttribute("aria-hidden", String(!open));
    elements["network-step"].hidden = !open || state.passwordStep;
    elements["password-step"].hidden = !open || !state.passwordStep;
    elements["selected-network"].textContent = state.selectedNetwork || "your network";
    if (open && state.passwordStep) {
      elements["wifi-password"].focus();
    }
  }

  function render() {
    renderStats();
    renderTelemetry();
    renderNetworks();
    renderSheet();
  }

  function openWifi() {
    state.sheetOpen = true;
    state.passwordStep = false;
    state.selectedNetwork = "";
    renderSheet();
    elements["network-list"].querySelector("button")?.focus();
  }

  function closeWifi() {
    state.sheetOpen = false;
    state.passwordStep = false;
    state.selectedNetwork = "";
    elements["wifi-password"].value = "";
    renderSheet();
    elements["wifi-action"].focus();
  }

  function selectNetwork(ssid) {
    const network = fixtures.networks.find((candidate) => candidate.ssid === ssid);
    if (!network) {
      return;
    }

    state.selectedNetwork = ssid;
    if (!network.secure) {
      connectSelectedNetwork();
      return;
    }

    state.passwordStep = true;
    renderSheet();
  }

  function connectSelectedNetwork() {
    state.connected = true;
    state.connectedNetwork = state.selectedNetwork;
    closeWifi();
    renderStats();
    renderTelemetry();
  }

  function connectWifi(event) {
    event.preventDefault();
    connectSelectedNetwork();
  }

  function reset() {
    state.connected = false;
    state.connectedNetwork = "";
    state.sheetOpen = false;
    state.passwordStep = false;
    state.selectedNetwork = "";
    elements["wifi-password"].value = "";
    render();
  }

  function bindEvents() {
    elements["wifi-action"].addEventListener("click", openWifi);
    elements["wifi-close"].addEventListener("click", closeWifi);
    elements["wifi-back"].addEventListener("click", () => {
      state.passwordStep = false;
      renderSheet();
    });
    elements["password-step"].addEventListener("submit", connectWifi);
  }

  function start() {
    cacheElements();
    bindEvents();
    render();
  }

  window.novaPreview = {
    state,
    openWifi,
    selectNetwork,
    connect: () => elements["password-step"].requestSubmit(),
    reset,
    render,
  };

  document.addEventListener("DOMContentLoaded", start, { once: true });
})();
