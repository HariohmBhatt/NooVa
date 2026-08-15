(function () {
  "use strict";

  const pageNames = [
    "home",
    "rooms",
    "room-detail",
    "assistant",
    "hub",
    "more",
    "diagnostics",
    "wifi",
    "ssh",
    "logs"
  ];
  const primaryPages = ["home", "rooms", "assistant", "hub", "more"];
  const { PREVIEW_CONFIG, keyboardRows, initialRooms, scenarioFixtures } = window.NovaFixtures;

  const state = {
    page: "home",
    scenario: "live",
    selectedRoomId: null,
    selectedNetworkIndex: null,
    keyboard: null,
    shift: false,
    lastFocusedElement: null,
    toastTimer: null,
    assistantTimer: null,
    wifiTimer: null,
    sshTimer: null,
    connectionTimers: [],
    lastLogSeconds: 6,
    lastAction: "Preview ready. Choose a scenario or touch the device.",
    hub: {
      status: "Live",
      grade: "Normal",
      valid: true,
      controlAvailable: true,
      host: "nova-hub.local",
      device: "hall-terminal-01",
      version: "0.4.0",
      latencyMs: 24,
      uptimeSeconds: 184203,
      cpuPercent: 12.4,
      memoryPercent: 37,
      diskPercent: 33,
      snapshotAgeSeconds: 2,
      memoryUsed: "3068 / 8192 MB",
      diskUsed: "42 / 128 GB",
      network: "84 KB/s in · 31 KB/s out",
      error: "",
      alert: "No alerts · everything is settled",
      services: {
        hubApi: "healthy",
        metrics: "healthy",
        homeControl: "planned"
      }
    },
    wifi: {
      status: "Connected",
      ssid: "Nova-24G",
      ip: "192.168.29.18",
      rssi: -47,
      scanning: false,
      networks: [
        { ssid: "Nova-24G", rssi: -47, encrypted: true },
        { ssid: "Nova-Guest", rssi: -61, encrypted: true },
        { ssid: "Workshop", rssi: -73, encrypted: false }
      ]
    },
    ssh: {
      configured: false,
      enabled: false,
      ready: false,
      username: "nova"
    },
    assistant: {
      phase: "idle",
      transcript: "“Turn on the living room lights.”",
      response: "Try: “Set a quiet scene for the evening.”"
    },
    control: {
      rooms: cloneRooms(),
      activeScene: "evening",
      pendingAction: null
    },
    logs: [
      { seconds: 0, level: "INFO", message: "Nova appliance booting" },
      { seconds: 1, level: "INFO", message: "Display and touch initialized" },
      { seconds: 2, level: "INFO", message: "Wi-Fi connected to Nova-24G" },
      { seconds: 3, level: "INFO", message: "Hub trust anchor loaded" },
      { seconds: 4, level: "INFO", message: "Dashboard ready for local control" },
      { seconds: 5, level: "INFO", message: "Health snapshot received · 24 ms" }
    ]
  };

  function cloneRooms() {
    return initialRooms.map(function (room) {
      return {
        id: room.id,
        name: room.name,
        icon: room.icon,
        temperature: room.temperature,
        summary: room.summary,
        devices: room.devices.map(function (device) {
          return Object.assign({}, device);
        })
      };
    });
  }

  const elements = {
    pages: Array.from(document.querySelectorAll(".screen-page")),
    navButtons: Array.from(document.querySelectorAll(".nav-button")),
    screenTime: document.getElementById("screen-time"),
    screenWifi: document.getElementById("screen-wifi"),
    homeSync: document.getElementById("home-sync"),
    homeClock: document.getElementById("home-clock"),
    homeState: document.getElementById("home-state"),
    homeLatency: document.getElementById("home-latency"),
    homeHealthIcon: document.getElementById("home-health-icon"),
    homeAssistant: document.getElementById("home-assistant"),
    summaryLights: document.getElementById("summary-lights"),
    summaryTemp: document.getElementById("summary-temp"),
    summaryDevices: document.getElementById("summary-devices"),
    sceneRow: document.getElementById("scene-row"),
    roomGrid: document.getElementById("room-grid"),
    roomCount: document.getElementById("room-count"),
    roomDetailKicker: document.getElementById("room-detail-kicker"),
    roomDetailTitle: document.getElementById("room-detail-title"),
    roomDetailTemp: document.getElementById("room-detail-temp"),
    roomDetailSummary: document.getElementById("room-detail-summary"),
    deviceList: document.getElementById("device-list"),
    assistantOrb: document.getElementById("assistant-orb"),
    assistantState: document.getElementById("assistant-state"),
    assistantCaption: document.getElementById("assistant-caption"),
    assistantSessionState: document.getElementById("assistant-session-state"),
    assistantTranscript: document.getElementById("assistant-transcript"),
    assistantResponse: document.getElementById("assistant-response"),
    assistantAction: document.getElementById("assistant-action"),
    hubHealthSummary: document.getElementById("hub-health-summary"),
    hubHealthGrade: document.getElementById("hub-health-grade"),
    hubHealthDetail: document.getElementById("hub-health-detail"),
    serviceList: document.getElementById("service-list"),
    hubCpu: document.getElementById("hub-cpu"),
    hubMemory: document.getElementById("hub-memory"),
    hubDisk: document.getElementById("hub-disk"),
    hubNetwork: document.getElementById("hub-network"),
    hubMetricsTitle: document.getElementById("hub-metrics-title"),
    cpuSpark: document.getElementById("cpu-spark"),
    memorySpark: document.getElementById("memory-spark"),
    hubFootnote: document.getElementById("hub-footnote"),
    moreSetupBanner: document.getElementById("more-setup-banner"),
    moreWifiLabel: document.getElementById("more-wifi-label"),
    moreSshLabel: document.getElementById("more-ssh-label"),
    moreLogLabel: document.getElementById("more-log-label"),
    diagnosticsGrade: document.getElementById("diagnostics-grade"),
    diagnosticList: document.getElementById("diagnostic-list"),
    wifiScan: document.getElementById("wifi-scan"),
    wifiCurrentSsid: document.getElementById("wifi-current-ssid"),
    wifiStatus: document.getElementById("wifi-status"),
    wifiList: document.getElementById("wifi-list"),
    wifiPasswordRow: document.getElementById("wifi-password-row"),
    wifiPassword: document.getElementById("wifi-password"),
    sshBadge: document.getElementById("ssh-badge"),
    sshStatus: document.getElementById("ssh-status"),
    sshAction: document.getElementById("ssh-action"),
    sshInstructions: document.getElementById("ssh-instructions"),
    sshPasswordRow: document.getElementById("ssh-password-row"),
    sshPassword: document.getElementById("ssh-password"),
    logText: document.getElementById("log-text"),
    bottomNav: document.getElementById("bottom-nav"),
    keyboard: document.getElementById("keyboard-overlay"),
    keyboardCaption: document.getElementById("keyboard-caption"),
    keyboardKeys: document.getElementById("keyboard-keys"),
    screenToast: document.getElementById("screen-toast"),
    stateReadout: document.getElementById("state-readout"),
    harnessHubPill: document.getElementById("harness-hub-pill"),
    harnessWifiPill: document.getElementById("harness-wifi-pill"),
    harnessPagePill: document.getElementById("harness-page-pill"),
    lastAction: document.getElementById("last-action"),
    eventTime: document.getElementById("event-time")
  };

  function setText(element, value) {
    if (element) {
      element.textContent = value;
    }
  }

  function nowTime() {
    return new Date().toTimeString().slice(0, 5);
  }

  function currentRoom() {
    return state.control.rooms.find(function (room) {
      return room.id === state.selectedRoomId;
    }) || state.control.rooms[0];
  }

  function hubTone() {
    if (state.hub.grade === "Warning") {
      return "warning";
    }
    if (state.hub.status === "Offline") {
      return "muted";
    }
    if (!state.hub.valid) {
      return "muted";
    }
    if (state.hub.grade === "Critical") {
      return "error";
    }
    return "good";
  }

  function isControlAvailable() {
    return state.hub.controlAvailable && state.hub.valid;
  }

  function formatUptime(seconds) {
    const days = Math.floor(seconds / PREVIEW_CONFIG.secondsPerDay);
    const hours = Math.floor((seconds % PREVIEW_CONFIG.secondsPerDay) / 3600);
    return days + "d " + String(hours).padStart(2, "0") + "h";
  }

  function snapshotDetail() {
    if (!state.hub.valid) {
      return "Waiting for Wi-Fi and automatic registration";
    }
    if (state.hub.status === "Offline") {
      return "Offline · last snapshot " + state.hub.snapshotAgeSeconds + "s ago · controls paused";
    }
    const freshness = state.hub.snapshotAgeSeconds >= PREVIEW_CONFIG.staleAfterSeconds
      ? "stale"
      : state.hub.snapshotAgeSeconds + "s ago";
    return "Connected · " + state.hub.latencyMs + " ms · snapshot " + freshness;
  }

  function snapshotIsCurrent() {
    return state.hub.valid && state.hub.snapshotAgeSeconds !== null && state.hub.snapshotAgeSeconds < PREVIEW_CONFIG.staleAfterSeconds;
  }

  function statusText() {
    if (!state.hub.valid) {
      return "SETUP REQUIRED";
    }
    if (state.hub.status === "Offline") {
      return "OFFLINE";
    }
    return state.hub.status.toUpperCase() + " / " + state.hub.grade.toUpperCase();
  }

  function announce(message) {
    state.lastAction = message;
    setText(elements.lastAction, message);
    setText(elements.eventTime, "NOW");
  }

  function log(level, message) {
    state.lastLogSeconds += 1;
    state.logs.push({ seconds: state.lastLogSeconds, level: level, message: message });
    if (state.logs.length > PREVIEW_CONFIG.maxLogEntries) {
      state.logs.shift();
    }
  }

  function showToast(message) {
    setText(elements.screenToast, message);
    elements.screenToast.classList.add("is-visible");
    window.clearTimeout(state.toastTimer);
    state.toastTimer = window.setTimeout(function () {
      elements.screenToast.classList.remove("is-visible");
    }, PREVIEW_CONFIG.toastDurationMs);
  }

  function showPage(page) {
    if (pageNames.indexOf(page) === -1) {
      return;
    }
    closeKeyboard(false);
    state.page = page;
    if (page === "room-detail" && !state.selectedRoomId) {
      state.selectedRoomId = state.control.rooms[0].id;
    }
    announce("Opened " + page.replace("room-detail", "room detail").toUpperCase());
    render();
    window.requestAnimationFrame(function () {
      const activePage = elements.pages.find(function (item) {
        return item.dataset.page === state.page;
      });
      const heading = activePage && activePage.querySelector("h2");
      if (heading) {
        heading.setAttribute("tabindex", "-1");
        heading.focus({ preventScroll: true });
      }
    });
  }

  function setStatusPill(element, value, tone) {
    setText(element, value);
    element.classList.remove("is-warning", "is-muted");
    if (tone === "warning") {
      element.classList.add("is-warning");
    }
    if (tone === "muted") {
      element.classList.add("is-muted");
    }
  }

  function renderPages() {
    const selectedPrimary = primaryPages.indexOf(state.page) !== -1
      ? state.page
      : state.page === "room-detail" ? "rooms" : "more";
    elements.pages.forEach(function (page) {
      const active = page.dataset.page === state.page;
      page.classList.toggle("is-active", active);
      page.setAttribute("aria-hidden", String(!active));
      if (!active) {
        page.setAttribute("inert", "");
      } else {
        page.removeAttribute("inert");
      }
    });
    elements.navButtons.forEach(function (button) {
      const active = button.dataset.target === selectedPrimary;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-current", active ? "page" : "false");
    });
  }

  function renderHome() {
    const tone = hubTone();
    const messages = {
      good: ["All systems calm", snapshotDetail(), "PREVIEW CLOCK"],
      warning: ["Hub is running warm", snapshotDetail(), "PREVIEW CLOCK"],
      muted: [
        state.hub.status === "Offline" ? "Hub connection paused" : state.hub.status === "Setup required" ? "Connect your home hub" : "Hub " + state.hub.status.toLowerCase(),
        state.hub.status === "Offline" ? "Offline · controls unavailable" : state.hub.status === "Setup required" ? "Wi-Fi setup required" : "Automatic registration in progress",
        state.hub.status === "Offline" ? "STALE" : state.hub.status === "Setup required" ? "SETUP" : "CONNECTING"
      ],
      error: ["Hub needs attention", state.hub.error || "Critical health alert", "ATTENTION"]
    }[tone];
    setText(elements.homeClock, nowTime());
    setText(elements.screenTime, nowTime());
    setText(elements.homeState, messages[0]);
    setText(elements.homeLatency, messages[1]);
    setText(elements.homeSync, messages[2]);
    elements.screenWifi.setAttribute("aria-label", "Wi-Fi " + state.wifi.status.toLowerCase());
    elements.screenWifi.classList.toggle("is-muted", state.wifi.status !== "Connected");
    elements.homeHealthIcon.classList.remove("is-warning", "is-muted", "is-error");
    if (tone === "warning") {
      elements.homeHealthIcon.classList.add("is-warning");
    } else if (tone === "muted") {
      elements.homeHealthIcon.classList.add("is-muted");
    } else if (tone === "error") {
      elements.homeHealthIcon.classList.add("is-error");
    }
    setText(elements.summaryLights, totalLightsOn() + " on");
    setText(elements.summaryTemp, state.hub.valid ? "21° · calm" : "-- · waiting");
    setText(elements.summaryDevices, totalDevices() + " online · " + (isControlAvailable() ? "0 attention" : "control paused"));
    elements.sceneRow.querySelectorAll("[data-scene]").forEach(function (button) {
      const active = button.dataset.scene === state.control.activeScene;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-pressed", String(active));
    });
    const assistantUnavailable = !isControlAvailable();
    elements.homeAssistant.setAttribute("aria-disabled", String(assistantUnavailable));
    setText(elements.homeAssistant.querySelector(".assistant-card-copy strong"), assistantUnavailable
      ? state.hub.status === "Setup required" ? "Finish setup to ask your home" : "Assistant unavailable offline"
      : "Ask your home anything");
    setText(elements.homeAssistant.querySelector(".hold-hint"), assistantUnavailable ? "OFF" : "TAP");
  }

  function totalLightsOn() {
    return state.control.rooms.reduce(function (count, room) {
      return count + room.devices.filter(function (device) {
        return device.icon === "light" && device.on;
      }).length;
    }, 0);
  }

  function totalDevices() {
    return state.control.rooms.reduce(function (count, room) {
      return count + room.devices.length;
    }, 0);
  }

  function renderRooms() {
    elements.roomGrid.replaceChildren();
    state.control.rooms.forEach(function (room) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "room-card";
      button.dataset.roomId = room.id;
      button.innerHTML = "<span class=\"room-card-icon\"><svg class=\"icon icon-sm\" aria-hidden=\"true\"><use href=\"#icon-" + room.icon + "\"></use></svg></span>" +
        "<span class=\"room-card-temperature\">" + room.temperature + "</span>" +
        "<strong>" + room.name + "</strong><small>" + room.summary + "</small>";
      elements.roomGrid.appendChild(button);
    });
    setText(elements.roomCount, String(state.control.rooms.length));
  }

  function renderRoomDetail() {
    const room = currentRoom();
    if (!room) {
      return;
    }
    setText(elements.roomDetailKicker, room.name.toUpperCase());
    setText(elements.roomDetailTitle, room.name);
    setText(elements.roomDetailTemp, room.temperature);
    setText(elements.roomDetailSummary, isControlAvailable() ? room.summary : "Control is paused until the hub is live again.");
    elements.deviceList.replaceChildren();
    room.devices.forEach(function (device) {
      const pending = state.control.pendingAction && state.control.pendingAction.deviceId === device.id;
      const button = document.createElement("button");
      button.type = "button";
      button.className = "device-row" + (device.on ? " is-on" : "");
      button.dataset.deviceId = device.id;
      button.disabled = pending || !isControlAvailable();
      button.setAttribute("aria-pressed", String(device.on));
      const detail = pending ? "Sending to hub…" : device.detail;
      button.innerHTML = "<span class=\"device-row-icon\"><svg class=\"icon icon-sm\" aria-hidden=\"true\"><use href=\"#icon-" + device.icon + "\"></use></svg></span>" +
        "<span class=\"device-row-copy\"><strong>" + device.name + "</strong><small>" + detail + "</small></span>" +
        "<span class=\"toggle\" aria-hidden=\"true\"></span>";
      elements.deviceList.appendChild(button);
    });
  }

  function assistantCopy() {
    if (!state.hub.valid) {
      return {
        state: "SETUP REQUIRED",
        caption: "Connect Wi-Fi and let the terminal register with the home hub.",
        session: "WAITING",
        response: "Voice sessions become available after the hub session is ready.",
        action: "SET UP WI-FI"
      };
    }
    if (!isControlAvailable()) {
      return {
        state: "HUB OFFLINE",
        caption: "Connect to the home hub before starting a voice session.",
        session: "UNAVAILABLE",
        response: "Voice requests are paused until the connection is current.",
        action: "UNAVAILABLE"
      };
    }
    const copies = {
      idle: {
        state: "READY TO HELP",
        caption: "Tap once, speak naturally, then tap when you are done.",
        session: "IDLE",
        response: state.assistant.response,
        action: "START LISTENING"
      },
      listening: {
        state: "LISTENING · AUDIO STREAMING",
        caption: "Tap again to stop capture. The microphone is active now.",
        session: "LISTENING",
        response: "Listening for a local request…",
        action: "STOP LISTENING"
      },
      processing: {
        state: "THINKING · CAPTURE STOPPED",
        caption: "The hub is validating the request and choosing a safe route.",
        session: "PROCESSING",
        response: "Preparing an approved response…",
        action: "PROCESSING…"
      },
      speaking: {
        state: "SPEAKING",
        caption: "Nova is replying through the local speaker.",
        session: "RESPONSE",
        response: "Mock hub approved the evening scene. The living room lights are on.",
        action: "START NEW SESSION"
      }
    };
    return copies[state.assistant.phase] || copies.idle;
  }

  function renderAssistant() {
    const copy = assistantCopy();
    const phase = isControlAvailable() ? state.assistant.phase : "unavailable";
    elements.assistantOrb.classList.remove("is-listening", "is-thinking", "is-speaking");
    if (phase === "listening") {
      elements.assistantOrb.classList.add("is-listening");
    } else if (phase === "processing") {
      elements.assistantOrb.classList.add("is-thinking");
    } else if (phase === "speaking") {
      elements.assistantOrb.classList.add("is-speaking");
    }
    setText(elements.assistantState, copy.state);
    setText(elements.assistantCaption, copy.caption);
    setText(elements.assistantSessionState, copy.session);
    setText(elements.assistantTranscript, state.assistant.transcript || "No transcript yet.");
    setText(elements.assistantResponse, copy.response);
    setText(elements.assistantAction, copy.action);
    elements.assistantAction.disabled = phase === "processing" || phase === "unavailable";
    elements.assistantOrb.disabled = phase === "processing" || phase === "unavailable";
    elements.assistantOrb.setAttribute("aria-pressed", String(phase === "listening"));
    elements.assistantOrb.setAttribute("aria-label", phase === "listening" ? "Stop listening" : "Start listening");
  }

  function renderHub() {
    const tone = hubTone();
    const summaryText = tone === "good"
      ? "ALL SYSTEMS NORMAL"
      : tone === "warning" ? "HEALTH NEEDS ATTENTION" : state.hub.status.toUpperCase();
    const detailText = state.hub.error
      ? state.hub.error + (state.hub.snapshotAgeSeconds === null ? "" : " · last snapshot " + state.hub.snapshotAgeSeconds + "s ago")
      : snapshotDetail();
    elements.hubHealthSummary.classList.remove("is-warning", "is-muted");
    if (tone === "warning") {
      elements.hubHealthSummary.classList.add("is-warning");
    } else if (tone === "muted") {
      elements.hubHealthSummary.classList.add("is-muted");
    }
    setText(elements.hubHealthGrade, summaryText);
    setText(elements.hubHealthDetail, detailText);
    const services = [
      ["Hub API", state.hub.services.hubApi],
      ["Metrics", state.hub.services.metrics],
      ["Home control", state.hub.services.homeControl],
      ["Trust anchor", state.hub.valid ? "loaded" : "waiting"]
    ];
    elements.serviceList.replaceChildren();
    services.forEach(function (service) {
      const item = document.createElement("div");
      const healthy = service[1] === "healthy" || service[1] === "ready" || service[1] === "loaded";
      const warning = service[1] === "slow";
      item.className = "service-item";
      item.innerHTML = "<span class=\"status-dot " + (healthy ? "is-live" : warning ? "is-warning" : "") + "\"></span><strong>" + service[0] + "</strong><small>" + service[1].toUpperCase() + "</small>";
      elements.serviceList.appendChild(item);
    });
    setText(elements.hubMetricsTitle, snapshotIsCurrent() ? "Live metrics" : "Last known metrics");
    setText(elements.hubCpu, snapshotIsCurrent() ? state.hub.cpuPercent.toFixed(1) + "%" : "--");
    setText(elements.hubMemory, snapshotIsCurrent() ? state.hub.memoryPercent + "%" : "--");
    setText(elements.hubDisk, snapshotIsCurrent() ? state.hub.diskPercent + "%" : "--");
    setText(elements.hubNetwork, snapshotIsCurrent() ? state.hub.network : "Values hidden until a current snapshot arrives");
    setText(elements.cpuSpark, snapshotIsCurrent() ? tone === "warning" ? "▃▄▅▆▇▆▇" : "▂▃▂▄▃▂▃" : "·······");
    setText(elements.memorySpark, snapshotIsCurrent() ? "▃▃▄▃▄▃▄" : "·······");
    setText(elements.hubFootnote, state.hub.valid
      ? "nova-hub.local · v" + state.hub.version + " · uptime " + formatUptime(state.hub.uptimeSeconds)
      : "Awaiting registration · trust boundary remains closed");
  }

  function renderMore() {
    setText(elements.moreWifiLabel, state.wifi.status === "Connected" ? state.wifi.ssid + " · " + state.wifi.rssi + " dBm" : state.wifi.status);
    setText(elements.moreSshLabel, state.ssh.ready ? "Ready · LAN only" : "LAN diagnostics");
    setText(elements.moreLogLabel, state.logs.length + " events · local buffer");
    if (!state.hub.valid) {
      elements.moreSetupBanner.classList.add("is-setup");
      elements.moreSetupBanner.querySelector("strong").textContent = "Finish terminal setup";
      elements.moreSetupBanner.querySelector("small").textContent = "Connect Wi-Fi to discover your home hub.";
    } else {
      elements.moreSetupBanner.classList.remove("is-setup");
      elements.moreSetupBanner.querySelector("strong").textContent = "Local by default";
      elements.moreSetupBanner.querySelector("small").textContent = "Your home data stays on your network.";
    }
  }

  function diagnosticRows() {
    const wifiState = state.wifi.status === "Connected" ? "ready" : state.wifi.status.toLowerCase();
    const hubState = state.hub.valid ? state.hub.status.toLowerCase() : "waiting";
    return [
      { label: "Display", detail: "ST7796 · 320 × 480", value: "READY", tone: "good" },
      { label: "Touch", detail: "FT6336 capacitive input", value: "READY", tone: "good" },
      { label: "Wi-Fi", detail: (state.wifi.ssid || "2.4 GHz network") + " · RSSI " + (state.wifi.rssi || "--") + " dBm", value: wifiState.toUpperCase(), tone: wifiState === "ready" ? "good" : "muted" },
      { label: "Home hub", detail: state.hub.host + " · " + (state.hub.valid ? state.hub.latencyMs + " ms" : "waiting"), value: hubState.toUpperCase(), tone: state.hub.valid ? hubTone() : "muted" },
      { label: "Firmware", detail: "Current preview build", value: "v" + state.hub.version, tone: "good" },
      { label: "Trust anchor", detail: "Caddy CA boundary", value: state.hub.valid ? "LOADED" : "WAITING", tone: state.hub.valid ? "good" : "muted" },
      { label: "Memory", detail: "8 MB octal PSRAM", value: "READY", tone: "good" },
      { label: "Audio", detail: "ES8311 application path", value: "UNAVAILABLE", tone: "warning" },
      { label: "IMU", detail: "QMI8658 motion path", value: "NOT IN UI", tone: "muted" },
      { label: "Storage", detail: "TF / SD interface", value: "NOT IN UI", tone: "muted" },
      { label: "SSH", detail: "LAN-only development tool", value: state.ssh.enabled ? "ENABLED" : "DISABLED", tone: state.ssh.enabled ? "good" : "muted" }
    ];
  }

  function renderDiagnostics() {
    const rows = diagnosticRows();
    const hasWarning = rows.some(function (row) { return row.tone === "warning"; });
    setText(elements.diagnosticsGrade, hasWarning ? "CHECK NOTES" : "READY");
    elements.diagnosticsGrade.classList.toggle("good", !hasWarning);
    elements.diagnosticList.replaceChildren();
    rows.forEach(function (row) {
      const item = document.createElement("div");
      item.className = "diagnostic-row";
      item.innerHTML = "<span class=\"status-dot " + (row.tone === "good" ? "is-live" : row.tone === "warning" ? "is-warning" : "") + "\"></span><span class=\"diagnostic-copy\"><strong>" + row.label + "</strong><small>" + row.detail + "</small></span><span class=\"diagnostic-value\">" + row.value + "</span>";
      elements.diagnosticList.appendChild(item);
    });
  }

  function renderWifi() {
    const connected = state.wifi.status === "Connected";
    const statusDetail = state.wifi.scanning
      ? "Scanning nearby 2.4 GHz networks…"
      : connected
        ? "Connected · " + state.wifi.ip + " · " + state.wifi.rssi + " dBm"
        : state.wifi.status + " · setup required";
    setText(elements.wifiCurrentSsid, state.wifi.ssid || "No network selected");
    setText(elements.wifiStatus, statusDetail);
    setText(elements.wifiScan, state.wifi.scanning ? "WAIT" : "SCAN");
    elements.wifiScan.disabled = state.wifi.scanning;
    elements.wifiList.replaceChildren();
    state.wifi.networks.forEach(function (network, index) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "wifi-network";
      button.dataset.networkIndex = String(index);
      button.innerHTML = '<span class="network-icon"><svg class="icon icon-xs" aria-hidden="true"><use href="#icon-wifi"></use></svg></span><span class="wifi-network-copy"><strong>' + network.ssid + '</strong><small>' + network.rssi + ' dBm · ' + (network.encrypted ? 'secured' : 'open') + '</small></span><span class="wifi-lock"><svg class="icon icon-xs" aria-hidden="true"><use href="#icon-' + (network.encrypted ? 'lock' : 'arrow') + '"></use></svg></span>';
      elements.wifiList.appendChild(button);
    });
    const keyboardVisible = state.keyboard === "wifi";
    elements.wifiList.classList.toggle("is-hidden", keyboardVisible);
    elements.wifiScan.classList.toggle("is-hidden", keyboardVisible);
    elements.wifiPasswordRow.classList.toggle("is-hidden", !keyboardVisible);
  }

  function renderSsh() {
    const status = state.ssh.ready ? "Ready · ssh nova@" + state.wifi.ip : state.ssh.configured ? "Disabled · credentials stored" : "Not configured · diagnostics only";
    setText(elements.sshStatus, status);
    setText(elements.sshAction, state.ssh.enabled ? "DISABLE ACCESS" : state.ssh.configured ? "ENABLE ACCESS" : "CONFIGURE ACCESS");
    setText(elements.sshBadge, state.ssh.enabled ? "READY" : "OFF");
    elements.sshBadge.classList.toggle("good", state.ssh.enabled);
    const keyboardVisible = state.keyboard === "ssh";
    elements.sshInstructions.classList.toggle("is-hidden", keyboardVisible);
    elements.sshAction.classList.toggle("is-hidden", keyboardVisible);
    elements.sshPasswordRow.classList.toggle("is-hidden", !keyboardVisible);
  }

  function renderLogs() {
    const wasPinned = elements.logText.scrollTop + elements.logText.clientHeight >= elements.logText.scrollHeight - 8;
    elements.logText.replaceChildren();
    state.logs.forEach(function (entry) {
      const line = document.createElement("span");
      line.className = "log-line " + entry.level.toLowerCase();
      line.innerHTML = String(entry.seconds).padStart(3, "0") + "s <span class=\"log-level\">" + entry.level.padEnd(5, " ") + "</span> " + entry.message;
      elements.logText.appendChild(line);
    });
    if (wasPinned || state.logs.length < 8) {
      elements.logText.scrollTop = elements.logText.scrollHeight;
    }
  }

  function renderKeyboard() {
    const visible = state.keyboard !== null;
    elements.keyboard.classList.toggle("is-hidden", !visible);
    elements.bottomNav.classList.toggle("is-hidden", visible);
    elements.keyboard.setAttribute("aria-hidden", String(!visible));
    if (!visible) {
      return;
    }
    setText(elements.keyboardCaption, state.keyboard === "wifi" ? "WI-FI PASSWORD" : "SSH PASSWORD");
    elements.keyboardKeys.replaceChildren();
    keyboardRows.forEach(function (row) {
      const rowElement = document.createElement("div");
      rowElement.className = "keyboard-row";
      row.forEach(function (key) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "keyboard-key";
        button.dataset.key = key;
        if (key === "shift" || key === "backspace") {
          button.classList.add("wide");
        }
        if (key === "space") {
          button.classList.add("space");
        }
        button.textContent = key === "backspace" ? "⌫" : key === "shift" ? "⇧" : key === "space" ? "SPACE" : state.shift ? key.toUpperCase() : key;
        rowElement.appendChild(button);
      });
      elements.keyboardKeys.appendChild(rowElement);
    });
  }

  function renderHarness() {
    setStatusPill(elements.harnessHubPill, "HUB " + statusText(), hubTone());
    setStatusPill(elements.harnessWifiPill, "WI-FI " + state.wifi.status.toUpperCase(), state.wifi.status === "Connected" ? "good" : "muted");
    setStatusPill(elements.harnessPagePill, state.page.replace("room-detail", "ROOM").toUpperCase(), "muted");
    const snapshot = {
      route: state.page,
      scenario: state.scenario,
      hub: {
        status: state.hub.status,
        health: state.hub.grade,
        control: isControlAvailable() ? "available" : "paused",
        latencyMs: state.hub.valid ? state.hub.latencyMs : null
      },
      wifi: {
        status: state.wifi.status,
        ssid: state.wifi.ssid || null,
        scanning: state.wifi.scanning
      },
      assistant: state.assistant.phase,
      ssh: state.ssh.enabled ? "ready" : "disabled",
      pendingAction: state.control.pendingAction,
      keyboard: state.keyboard,
      lastAction: state.lastAction
    };
    setText(elements.stateReadout, JSON.stringify(snapshot, null, 2));
    setText(elements.lastAction, state.lastAction);
  }

  function render() {
    renderPages();
    renderHome();
    renderRooms();
    renderRoomDetail();
    renderAssistant();
    renderHub();
    renderMore();
    renderDiagnostics();
    renderWifi();
    renderSsh();
    renderLogs();
    renderKeyboard();
    renderHarness();
  }

  function activeInput() {
    if (state.keyboard === "wifi") {
      return elements.wifiPassword;
    }
    if (state.keyboard === "ssh") {
      return elements.sshPassword;
    }
    return null;
  }

  function showKeyboard(mode, networkIndex) {
    state.lastFocusedElement = document.activeElement;
    state.keyboard = mode;
    state.selectedNetworkIndex = networkIndex === undefined ? null : networkIndex;
    state.shift = false;
    activeInput().value = "";
    announce(mode === "wifi" ? "Editing Wi-Fi password" : "Editing SSH password");
    render();
    window.requestAnimationFrame(function () {
      activeInput().focus();
    });
  }

  function closeKeyboard(shouldRender) {
    const input = activeInput();
    if (input) {
      input.value = "";
    }
    state.keyboard = null;
    state.selectedNetworkIndex = null;
    state.shift = false;
    if (shouldRender !== false) {
      render();
    }
    if (state.lastFocusedElement && typeof state.lastFocusedElement.focus === "function") {
      state.lastFocusedElement.focus();
      state.lastFocusedElement = null;
    }
  }

  function handleKey(key) {
    const input = activeInput();
    if (!input) {
      return;
    }
    if (key === "shift") {
      state.shift = !state.shift;
      render();
      input.focus();
      return;
    }
    if (key === "backspace") {
      input.value = input.value.slice(0, -1);
    } else if (key === "space") {
      input.value += " ";
    } else if (key === "123") {
      input.value += "123";
    } else {
      input.value += state.shift ? key.toUpperCase() : key;
    }
    state.lastAction = "Password edited";
    renderHarness();
    input.focus();
  }

  function finishWifiSetup() {
    const selected = state.wifi.networks[state.selectedNetworkIndex];
    if (!selected) {
      closeKeyboard();
      return;
    }
    if (selected.encrypted && elements.wifiPassword.value.length < 1) {
      showToast("Enter a password before connecting.");
      elements.wifiPassword.focus();
      return;
    }
    state.connectionTimers.forEach(function (timer) {
      window.clearTimeout(timer);
    });
    state.connectionTimers = [];
    state.wifi.status = "Connecting";
    state.wifi.ssid = selected.ssid;
    state.wifi.ip = "0.0.0.0";
    state.hub.valid = false;
    state.hub.status = "Discovering";
    state.hub.controlAvailable = false;
    announce("Connecting to " + selected.ssid);
    log("INFO", "Wi-Fi credentials accepted for " + selected.ssid);
    closeKeyboard(false);
    render();
    state.connectionTimers.push(window.setTimeout(function () {
      state.wifi.status = "Connected";
      state.wifi.ip = "192.168.29.18";
      state.wifi.rssi = selected.rssi;
      state.hub.status = "Registering";
      announce("Wi-Fi connected · registering this terminal");
      log("INFO", "Wi-Fi connection established");
      render();
    }, PREVIEW_CONFIG.wifiTransitionMs));
    state.connectionTimers.push(window.setTimeout(function () {
      state.hub.status = "Connecting";
      announce("Opening the trusted hub session");
      log("INFO", "device.hello sent · waiting for session.ready");
      render();
    }, PREVIEW_CONFIG.wifiTransitionMs * 2));
    state.connectionTimers.push(window.setTimeout(function () {
      Object.assign(state.hub, scenarioFixtures.live.hub, {
        services: Object.assign({}, scenarioFixtures.live.hub.services)
      });
      state.scenario = "live";
      state.connectionTimers = [];
      announce("Hub session ready · home state synchronized");
      log("INFO", "session.ready received · health snapshot current");
      render();
    }, PREVIEW_CONFIG.wifiTransitionMs * 3));
  }

  function finishSshSetup() {
    if (elements.sshPassword.value.length < PREVIEW_CONFIG.minPasswordLength) {
      showToast("Use at least four characters for this mock password.");
      elements.sshPassword.focus();
      return;
    }
    state.ssh.configured = true;
    state.ssh.enabled = true;
    state.ssh.ready = false;
    announce("SSH credentials stored. Starting LAN diagnostics…");
    log("INFO", "SSH credentials accepted for user nova");
    closeKeyboard(false);
    render();
    state.sshTimer = window.setTimeout(function () {
      state.ssh.ready = true;
      announce("SSH diagnostics ready on the trusted LAN");
      log("INFO", "SSH diagnostics listener is ready");
      render();
    }, PREVIEW_CONFIG.sshTransitionMs);
  }

  function chooseScenario(scenario) {
    const selectedScenario = scenarioFixtures[scenario] ? scenario : "setup";
    const fixture = scenarioFixtures[selectedScenario];
    state.scenario = selectedScenario;
    window.clearTimeout(state.wifiTimer);
    state.connectionTimers.forEach(function (timer) {
      window.clearTimeout(timer);
    });
    state.connectionTimers = [];
    window.clearTimeout(state.assistantTimer);
    window.clearTimeout(state.sshTimer);
    Object.assign(state.hub, fixture.hub, {
      services: Object.assign({}, fixture.hub.services)
    });
    Object.assign(state.wifi, fixture.wifi, { scanning: false });
    state.assistant.phase = "idle";
    state.control.pendingAction = null;
    announce("Selected " + selectedScenario.toUpperCase() + " mock scenario");
    log("INFO", "Mock hub state changed to " + selectedScenario);
    document.querySelectorAll("[data-scenario]").forEach(function (button) {
      const selected = button.dataset.scenario === selectedScenario;
      button.classList.toggle("is-selected", selected);
      button.setAttribute("aria-pressed", String(selected));
    });
    showToast(fixture.toast);
    render();
  }

  function chooseScene(scene) {
    if (!isControlAvailable()) {
      showToast("Scenes are paused until the hub is current.");
      return;
    }
    state.control.activeScene = scene;
    announce(scene.charAt(0).toUpperCase() + scene.slice(1) + " scene request sent to the mock hub");
    log("INFO", "Scene " + scene + " approved by local mock policy");
    showToast(scene.charAt(0).toUpperCase() + scene.slice(1) + " scene is active");
    render();
  }

  function chooseRoom(roomId) {
    state.selectedRoomId = roomId;
    const room = currentRoom();
    announce("Opened " + room.name);
    showPage("room-detail");
  }

  function toggleDevice(deviceId) {
    const room = currentRoom();
    const device = room && room.devices.find(function (item) { return item.id === deviceId; });
    if (!device) {
      return;
    }
    if (!isControlAvailable()) {
      showToast(state.hub.status === "Setup required" ? "Finish terminal setup before controlling devices." : "Control paused while the hub is offline.");
      return;
    }
    if (state.control.pendingAction) {
      return;
    }
    state.control.pendingAction = { deviceId: device.id, roomId: room.id, action: device.on ? "turn_off" : "turn_on" };
    announce("Sending " + (device.on ? "off" : "on") + " request for " + device.name);
    log("INFO", "Mock device request queued · " + device.id);
    render();
    window.setTimeout(function () {
      device.on = !device.on;
      state.control.pendingAction = null;
      announce(device.name + " is now " + (device.on ? "on" : "off"));
      log("INFO", "Mock device state acknowledged · " + device.id);
      showToast(device.name + " turned " + (device.on ? "on" : "off"));
      render();
    }, PREVIEW_CONFIG.deviceActionMs);
  }

  function startAssistant() {
    if (!isControlAvailable()) {
      showToast(state.hub.status === "Setup required" ? "Finish terminal setup before starting a voice session." : "Voice sessions are paused while the hub is offline.");
      return;
    }
    if (state.assistant.phase === "processing") {
      return;
    }
    if (state.assistant.phase === "listening") {
      state.assistant.phase = "processing";
      announce("Capture stopped. Hub is thinking…");
      log("INFO", "Voice session audio capture stopped");
      render();
      state.assistantTimer = window.setTimeout(function () {
        state.assistant.phase = "speaking";
        state.assistant.transcript = "“Turn on the living room lights.”";
        state.assistant.response = "Mock hub approved the living room light action.";
        announce("Voice response ready · local action approved");
        log("INFO", "Voice intent validated · living room lights");
        showToast("Action approved by the mock hub");
        render();
        state.assistantTimer = window.setTimeout(function () {
          state.assistant.phase = "idle";
          render();
        }, PREVIEW_CONFIG.assistantResponseMs);
      }, PREVIEW_CONFIG.assistantThinkMs);
      return;
    }
    state.assistant.phase = "listening";
    announce("Listening. Audio is streaming for this session only.");
    log("INFO", "Voice session started · microphone active");
    showToast("Listening · tap when you are done");
    render();
  }

  function handleSshAction() {
    if (state.ssh.enabled) {
      state.ssh.enabled = false;
      state.ssh.ready = false;
      announce("SSH diagnostics disabled");
      log("INFO", "SSH diagnostics disabled");
      render();
      return;
    }
    if (state.ssh.configured) {
      state.ssh.enabled = true;
      state.ssh.ready = false;
      announce("SSH diagnostics starting");
      log("INFO", "SSH diagnostics enabled");
      render();
      state.sshTimer = window.setTimeout(function () {
        state.ssh.ready = true;
        render();
      }, PREVIEW_CONFIG.sshEnableMs);
      return;
    }
    showKeyboard("ssh");
  }

  function scanWifi() {
    if (state.wifi.scanning) {
      return;
    }
    state.wifi.scanning = true;
    announce("Scanning nearby 2.4 GHz networks");
    log("INFO", "Wi-Fi scan started");
    render();
    state.wifiTimer = window.setTimeout(function () {
      state.wifi.scanning = false;
      announce("Wi-Fi scan complete · " + state.wifi.networks.length + " networks found");
      log("INFO", "Wi-Fi scan found " + state.wifi.networks.length + " networks");
      render();
    }, PREVIEW_CONFIG.wifiTransitionMs);
  }

  document.addEventListener("click", function (event) {
    const target = event.target.closest("button, [data-target]");
    if (!target) {
      return;
    }
    if (target.dataset.target) {
      if (target === elements.homeAssistant && !isControlAvailable()) {
        showToast(state.hub.status === "Setup required" ? "Finish terminal setup before using the assistant." : "Assistant unavailable while the hub is offline.");
        return;
      }
      showPage(target.dataset.target);
      return;
    }
    if (target.dataset.scenario) {
      chooseScenario(target.dataset.scenario);
      return;
    }
    if (target.dataset.scene) {
      chooseScene(target.dataset.scene);
      return;
    }
    if (target.dataset.roomId) {
      chooseRoom(target.dataset.roomId);
      return;
    }
    if (target.dataset.deviceId) {
      toggleDevice(target.dataset.deviceId);
      return;
    }
    if (target.dataset.networkIndex) {
      const network = state.wifi.networks[Number(target.dataset.networkIndex)];
      if (network && network.encrypted) {
        showKeyboard("wifi", Number(target.dataset.networkIndex));
      } else if (network) {
        state.selectedNetworkIndex = Number(target.dataset.networkIndex);
        finishWifiSetup();
      }
      return;
    }
    if (target.dataset.key) {
      handleKey(target.dataset.key);
      return;
    }
    if (target.dataset.keyAction === "cancel") {
      announce("Password entry cancelled");
      closeKeyboard();
      return;
    }
    if (target.dataset.keyAction === "clear") {
      activeInput().value = "";
      announce("Password field cleared");
      renderHarness();
      activeInput().focus();
      return;
    }
    if (target.dataset.keyAction === "ready") {
      if (state.keyboard === "wifi") {
        finishWifiSetup();
      } else {
        finishSshSetup();
      }
      return;
    }
    if (target === elements.wifiScan) {
      scanWifi();
      return;
    }
    if (target === elements.sshAction) {
      handleSshAction();
      return;
    }
    if (target === elements.assistantOrb || target === elements.assistantAction) {
      startAssistant();
      return;
    }
    if (target.id === "reset-demo") {
      window.location.reload();
    }
  });

  [elements.wifiPassword, elements.sshPassword].forEach(function (input) {
    input.addEventListener("input", function () {
      state.lastAction = "Password edited";
      renderHarness();
    });
    input.addEventListener("keydown", function (event) {
      if (event.key === "Enter" && state.keyboard !== null) {
        event.preventDefault();
        if (state.keyboard === "wifi") {
          finishWifiSetup();
        } else {
          finishSshSetup();
        }
      }
      if (event.key === "Escape") {
        closeKeyboard();
      }
    });
  });

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && state.keyboard !== null) {
      closeKeyboard();
    }
    if (event.key === "Tab" && state.keyboard !== null) {
      const focusable = Array.from(elements.keyboard.querySelectorAll("button, input"));
      if (focusable.length === 0) {
        return;
      }
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    }
  });

  window.setInterval(function () {
    setText(elements.screenTime, nowTime());
    if (state.page === "home") {
      setText(elements.homeClock, nowTime());
    }
  }, PREVIEW_CONFIG.clockRefreshMs);

  window.novaPreview = {
    state: state,
    chooseScenario: chooseScenario,
    showPage: showPage,
    render: render
  };

  render();
})();
