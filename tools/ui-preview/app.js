(function () {
  "use strict";

  const pageNames = ["home", "server", "setup", "diagnostics", "wifi", "ssh", "logs"];
  const keyboardRows = [
    ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
    ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
    ["shift", "z", "x", "c", "v", "b", "n", "m", "backspace"],
    ["123", "space"]
  ];

  const state = {
    page: "home",
    scenario: "live",
    lastAction: "Booted local replica",
    keyboard: null,
    selectedNetworkIndex: null,
    shift: false,
    hub: {
      valid: true,
      state: "Live",
      grade: "Normal",
      host: "nova-hub.local",
      device: "nova-esp32s3-01",
      version: "0.4.0",
      latencyMs: 24,
      uptimeSeconds: 184203,
      cpuPercent: 12.4,
      memoryUsed: "3068 / 8192 MB",
      diskUsed: "42 / 128 GB",
      network: "eth0",
      rx: 84,
      tx: 31,
      error: "",
      alert: "Alerts: none"
    },
    wifi: {
      state: "Connected",
      ssid: "Nova-5G",
      ip: "192.168.29.18",
      rssi: -47,
      scanning: false,
      networks: [
        { ssid: "Nova-5G", rssi: -47, encrypted: true },
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
    logs: [
      { seconds: 0, level: "INFO", message: "Nova appliance booting" },
      { seconds: 1, level: "INFO", message: "Display initialized" },
      { seconds: 1, level: "INFO", message: "Touch initialized" },
      { seconds: 2, level: "INFO", message: "Wi-Fi connected to Nova-5G" },
      { seconds: 3, level: "INFO", message: "Dashboard UI initialized" },
      { seconds: 4, level: "INFO", message: "Hub metrics stream is live" }
    ]
  };

  const elements = {
    pages: Array.from(document.querySelectorAll(".page")),
    nav: document.getElementById("bottom-nav"),
    navButtons: Array.from(document.querySelectorAll(".nav-button")),
    homeClock: document.getElementById("home-clock"),
    homeState: document.getElementById("home-state"),
    homeLatency: document.getElementById("home-latency"),
    homeCpu: document.getElementById("home-cpu"),
    homeMemory: document.getElementById("home-memory"),
    homeDisk: document.getElementById("home-disk"),
    homeNetwork: document.getElementById("home-network"),
    serverStatus: document.getElementById("server-status"),
    serverMetrics: document.getElementById("server-metrics"),
    setupStatus: document.getElementById("setup-status"),
    diagnosticsStatus: document.getElementById("diagnostics-status"),
    wifiStatus: document.getElementById("wifi-status"),
    wifiList: document.getElementById("wifi-list"),
    wifiScan: document.getElementById("wifi-scan"),
    wifiPasswordRow: document.getElementById("wifi-password-row"),
    wifiPassword: document.getElementById("wifi-password"),
    sshStatus: document.getElementById("ssh-status"),
    sshAction: document.getElementById("ssh-action"),
    sshInstructions: document.getElementById("ssh-instructions"),
    sshPasswordRow: document.getElementById("ssh-password-row"),
    sshPassword: document.getElementById("ssh-password"),
    logText: document.getElementById("log-text"),
    keyboard: document.getElementById("keyboard-overlay"),
    keyboardCaption: document.getElementById("keyboard-caption"),
    keyboardKeys: document.getElementById("keyboard-keys"),
    stateReadout: document.getElementById("state-readout")
  };

  function setText(element, value) {
    element.textContent = value;
  }

  function setTone(element, tone) {
    element.classList.remove("tone-good", "tone-warning", "tone-error", "tone-muted");
    element.classList.add("tone-" + tone);
  }

  function toneForGrade(grade) {
    if (grade === "Normal") {
      return "good";
    }
    if (grade === "Warning") {
      return "warning";
    }
    if (grade === "Critical") {
      return "error";
    }
    return "muted";
  }

  function nowTime() {
    return new Date().toTimeString().slice(0, 8);
  }

  function hubLabel() {
    return state.hub.state.toUpperCase();
  }

  function log(level, message) {
    state.logs.push({
      seconds: state.logs.length + 4,
      level: level,
      message: message
    });
    if (state.logs.length > 12) {
      state.logs.shift();
    }
  }

  function renderHome() {
    if (!state.hub.valid) {
      setText(elements.homeClock, "SERVER TIME  --");
      setText(elements.homeState, "HUB SETUP REQUIRED");
      setTone(elements.homeState, "muted");
      setText(elements.homeLatency, "Waiting for live server metrics");
      setText(elements.homeCpu, "--");
      setText(elements.homeMemory, "--");
      setText(elements.homeDisk, "--");
      setText(elements.homeNetwork, "--");
      return;
    }

    setText(elements.homeClock, "SERVER " + nowTime());
    setText(elements.homeState, "HUB " + hubLabel() + " / " + state.hub.grade.toUpperCase());
    setTone(elements.homeState, toneForGrade(state.hub.grade));
    setText(
      elements.homeLatency,
      "Latency " + state.hub.latencyMs + " ms  /  " + state.hub.grade.toUpperCase()
    );
    setText(elements.homeCpu, state.hub.cpuPercent.toFixed(1) + "%");
    setText(elements.homeMemory, state.hub.memoryUsed);
    setText(elements.homeDisk, state.hub.diskUsed);
    setText(elements.homeNetwork, state.hub.rx + "/" + state.hub.tx + " KB/s");
  }

  function renderServer() {
    setText(
      elements.serverStatus,
      "State: " + hubLabel() +
        "\nHost: " + state.hub.host +
        "\nDevice: " + (state.hub.valid ? state.hub.device : "NOT REGISTERED") +
        "\nCA: LOADED"
    );

    if (!state.hub.valid) {
      setText(elements.serverMetrics, "Waiting for a live health snapshot.");
      return;
    }

    const trend = state.scenario === "degraded"
      ? "CPU: #*o.  RAM: ##*o  DISK: .o*#"
      : "CPU: .o*#  RAM: .o*#  DISK: oo*#";
    setText(
      elements.serverMetrics,
      "Version: " + state.hub.version +
        "\nHealth: " + state.hub.grade.toUpperCase() +
        "\nUptime: " + state.hub.uptimeSeconds + " s" +
        "\nCPU: " + state.hub.cpuPercent.toFixed(1) + "%" +
        "\nRAM: " + state.hub.memoryUsed +
        "\nDisk: " + state.hub.diskUsed +
        "\nNetwork: " + state.hub.network +
        "\nRX/TX: " + state.hub.rx + " / " + state.hub.tx + " KB/s" +
        "\n" + (state.hub.error || "Metrics current") +
        "\n" + state.hub.alert +
        "\nTrends " + trend +
        "\nServices: hub=UP metrics=" + (state.hub.valid ? "CURRENT" : "WAITING")
    );
  }

  function renderSetup() {
    let text = "Hub: " + hubLabel() + "\nWi-Fi: " + state.wifi.state.toUpperCase();
    if (state.hub.error) {
      text += "\n" + state.hub.error;
    }
    setText(elements.setupStatus, text);
  }

  function renderDiagnostics() {
    setText(
      elements.diagnosticsStatus,
      "DISPLAY  READY" +
        "\nTOUCH    READY" +
        "\nWI-FI    " + state.wifi.state.toUpperCase() +
        "\nHUB      " + hubLabel() +
        "\nHEALTH   " + state.hub.grade.toUpperCase() +
        "\nCA       LOADED" +
        "\nSSH      " + (state.ssh.enabled ? "ENABLED" : "DISABLED") +
        "\nPSRAM    READY" +
        "\n\nProduction SSH/OTA is disabled by the secure rollout policy."
    );
  }

  function renderWifi() {
    setText(
      elements.wifiStatus,
      "State: " + state.wifi.state.toUpperCase() +
        "\nSSID: " + (state.wifi.ssid || "--") +
        "\nIP: " + state.wifi.ip + "  RSSI: " + state.wifi.rssi
    );
    setText(elements.wifiScan, state.wifi.scanning ? "WAIT" : "SCAN");
    elements.wifiList.replaceChildren();
    state.wifi.networks.forEach(function (network, index) {
      const button = document.createElement("button");
      button.className = "wifi-network";
      button.type = "button";
      button.dataset.networkIndex = String(index);
      button.textContent =
        network.ssid + "  " + network.rssi + " dBm " + (network.encrypted ? "LOCK" : "OPEN");
      elements.wifiList.appendChild(button);
    });
    elements.wifiList.classList.toggle("is-hidden", state.keyboard === "wifi");
    elements.wifiScan.classList.toggle("is-hidden", state.keyboard === "wifi");
    elements.wifiPasswordRow.classList.toggle("is-hidden", state.keyboard !== "wifi");
  }

  function renderSsh() {
    if (!state.ssh.configured) {
      setText(elements.sshStatus, "Status: NOT CONFIGURED\nUser: nova");
      setText(elements.sshAction, "SET UP");
    } else if (state.ssh.enabled) {
      setText(
        elements.sshStatus,
        "Status: " + (state.ssh.ready ? "READY" : "STARTING") +
          "\nssh " + state.ssh.username + "@" + state.wifi.ip
      );
      setText(elements.sshAction, "DISABLE");
    } else {
      setText(elements.sshStatus, "Status: DISABLED\nUser: " + state.ssh.username);
      setText(elements.sshAction, "ENABLE");
    }
    elements.sshInstructions.classList.toggle("is-hidden", state.keyboard === "ssh");
    elements.sshAction.classList.toggle("is-hidden", state.keyboard === "ssh");
    elements.sshPasswordRow.classList.toggle("is-hidden", state.keyboard !== "ssh");
  }

  function renderLogs() {
    const lines = state.logs.map(function (entry) {
      return String(entry.seconds).padStart(3, "0") + "s " +
        entry.level.padEnd(5, " ") + " " + entry.message;
    });
    setText(elements.logText, lines.join("\n") || "No diagnostic messages recorded.");
    elements.logText.scrollTop = elements.logText.scrollHeight;
  }

  function renderPages() {
    elements.pages.forEach(function (page) {
      page.classList.toggle("is-active", page.dataset.page === state.page);
    });
    elements.navButtons.forEach(function (button) {
      button.classList.toggle("is-active", button.dataset.target === state.page);
    });
  }

  function renderKeyboard() {
    const visible = state.keyboard !== null;
    elements.keyboard.classList.toggle("is-hidden", !visible);
    elements.nav.classList.toggle("is-hidden", visible);
    if (!visible) {
      return;
    }

    setText(
      elements.keyboardCaption,
      state.keyboard === "wifi" ? "WI-FI PASSWORD" : "SSH PASSWORD"
    );
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
        if (key === "shift" && state.shift) {
          button.classList.add("tone-good");
        }
        button.textContent = key === "backspace" ? "⌫" :
          key === "shift" ? "⇧" : key === "space" ? "SPACE" : key;
        if (state.shift && key.length === 1) {
          button.textContent = key.toUpperCase();
        }
        rowElement.appendChild(button);
      });
      elements.keyboardKeys.appendChild(rowElement);
    });
  }

  function renderHarness() {
    const snapshot = {
      page: state.page,
      scenario: state.scenario,
      hub: {
        state: state.hub.state,
        grade: state.hub.grade,
        valid: state.hub.valid
      },
      wifi: {
        state: state.wifi.state,
        ssid: state.wifi.ssid,
        scanning: state.wifi.scanning
      },
      ssh: {
        configured: state.ssh.configured,
        enabled: state.ssh.enabled,
        ready: state.ssh.ready
      },
      keyboard: state.keyboard,
      selectedNetwork: state.selectedNetworkIndex,
      lastAction: state.lastAction
    };
    setText(elements.stateReadout, JSON.stringify(snapshot, null, 2));
  }

  function render() {
    renderPages();
    renderHome();
    renderServer();
    renderSetup();
    renderDiagnostics();
    renderWifi();
    renderSsh();
    renderLogs();
    renderKeyboard();
    renderHarness();
  }

  function activeInput() {
    return state.keyboard === "wifi" ? elements.wifiPassword : elements.sshPassword;
  }

  function activeInputFor(mode) {
    return mode === "wifi" ? elements.wifiPassword : elements.sshPassword;
  }

  function showPage(page) {
    if (pageNames.indexOf(page) === -1) {
      return;
    }
    closeKeyboard(false);
    state.page = page;
    state.lastAction = "Opened " + page.toUpperCase() + " page";
    render();
  }

  function showKeyboard(mode, networkIndex) {
    state.keyboard = mode;
    state.selectedNetworkIndex = networkIndex === undefined ? null : networkIndex;
    state.shift = false;
    activeInputFor(mode).value = "";
    state.lastAction = mode === "wifi"
      ? "Editing Wi-Fi password"
      : "Editing SSH password";
    render();
    window.requestAnimationFrame(function () {
      activeInput().focus();
    });
  }

  function closeKeyboard(shouldRender) {
    state.keyboard = null;
    state.selectedNetworkIndex = null;
    state.shift = false;
    if (shouldRender !== false) {
      render();
    }
  }

  function finishWifiSetup() {
    const selected = state.wifi.networks[state.selectedNetworkIndex];
    if (!selected) {
      closeKeyboard();
      return;
    }
    state.wifi.state = "Connecting";
    state.wifi.ssid = selected.ssid;
    state.wifi.ip = "0.0.0.0";
    state.lastAction = "Connecting to " + selected.ssid;
    log("INFO", "Wi-Fi credentials accepted for " + selected.ssid);
    closeKeyboard();
    window.setTimeout(function () {
      state.wifi.state = "Connected";
      state.wifi.ip = "192.168.29.18";
      state.wifi.rssi = selected.rssi;
      state.lastAction = "Wi-Fi connected to " + selected.ssid;
      log("INFO", "Wi-Fi connection established");
      render();
    }, 650);
  }

  function finishSshSetup() {
    state.ssh.configured = true;
    state.ssh.enabled = true;
    state.ssh.ready = true;
    state.lastAction = "SSH configured and enabled";
    log("INFO", "SSH diagnostics enabled for user nova");
    closeKeyboard();
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
    input.dispatchEvent(new Event("input", { bubbles: true }));
    input.focus();
  }

  function chooseScenario(scenario) {
    state.scenario = scenario;
    if (scenario === "live") {
      state.hub.valid = true;
      state.hub.state = "Live";
      state.hub.grade = "Normal";
      state.hub.latencyMs = 24;
      state.hub.cpuPercent = 12.4;
      state.hub.error = "";
      state.hub.alert = "Alerts: none";
    } else if (scenario === "degraded") {
      state.hub.valid = true;
      state.hub.state = "Degraded";
      state.hub.grade = "Warning";
      state.hub.latencyMs = 186;
      state.hub.cpuPercent = 78.6;
      state.hub.error = "Metrics delayed: collector response is slow";
      state.hub.alert = "Alert: cpu WARNING 78.6";
    } else {
      state.hub.valid = false;
      state.hub.state = "Setup required";
      state.hub.grade = "Unknown";
      state.hub.error = "";
      state.hub.alert = "Alerts: none";
    }
    state.lastAction = "Selected " + scenario.toUpperCase() + " mock scenario";
    document.querySelectorAll("[data-scenario]").forEach(function (button) {
      button.classList.toggle("is-selected", button.dataset.scenario === scenario);
    });
    log("INFO", "Mock hub state changed to " + scenario);
    render();
  }

  elements.navButtons.forEach(function (button) {
    button.addEventListener("click", function () {
      showPage(button.dataset.target);
    });
  });

  document.getElementById("setup-wifi").addEventListener("click", function () {
    showPage("wifi");
  });

  document.getElementById("setup-ssh").addEventListener("click", function () {
    showPage("ssh");
  });

  document.getElementById("wifi-back").addEventListener("click", function () {
    showPage("setup");
  });

  document.getElementById("ssh-back").addEventListener("click", function () {
    showPage("setup");
  });

  elements.wifiList.addEventListener("click", function (event) {
    const button = event.target.closest("[data-network-index]");
    if (!button) {
      return;
    }
    showKeyboard("wifi", Number(button.dataset.networkIndex));
  });

  elements.wifiScan.addEventListener("click", function () {
    if (state.wifi.scanning) {
      return;
    }
    state.wifi.scanning = true;
    state.lastAction = "Scanning for Wi-Fi networks";
    log("INFO", "Wi-Fi scan started");
    render();
    window.setTimeout(function () {
      state.wifi.scanning = false;
      state.lastAction = "Wi-Fi scan complete";
      log("INFO", "Wi-Fi scan found " + state.wifi.networks.length + " networks");
      render();
    }, 650);
  });

  document.getElementById("ssh-action").addEventListener("click", function () {
    if (state.ssh.enabled) {
      state.ssh.enabled = false;
      state.ssh.ready = false;
      state.lastAction = "SSH disabled";
      log("INFO", "SSH diagnostics disabled");
    } else if (state.ssh.configured) {
      state.ssh.enabled = true;
      state.ssh.ready = true;
      state.lastAction = "SSH enabled";
      log("INFO", "SSH diagnostics enabled");
    } else {
      showKeyboard("ssh");
      return;
    }
    render();
  });

  elements.keyboardKeys.addEventListener("click", function (event) {
    const button = event.target.closest("[data-key]");
    if (button) {
      handleKey(button.dataset.key);
    }
  });

  document.querySelector(".keyboard-actions").addEventListener("click", function (event) {
    const action = event.target.closest("[data-key-action]");
    if (!action) {
      return;
    }
    if (action.dataset.keyAction === "cancel") {
      state.lastAction = "Cancelled password entry";
      closeKeyboard();
    } else if (state.keyboard === "wifi") {
      finishWifiSetup();
    } else {
      finishSshSetup();
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

  document.querySelectorAll("[data-scenario]").forEach(function (button) {
    button.addEventListener("click", function () {
      chooseScenario(button.dataset.scenario);
    });
  });

  document.getElementById("reset-demo").addEventListener("click", function () {
    window.location.reload();
  });

  render();
})();
