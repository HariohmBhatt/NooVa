/*
 * PROTOTYPE ONLY — deliberately dependency-free and not production firmware.
 * The fixtures expose every required display state and bounded-data edge case.
 */

const variants = {
  A: { name: "Beacon", render: renderBeacon },
  B: { name: "Instrument", render: renderInstrument },
  C: { name: "Signal path", render: renderSignalPath },
  D: { name: "Sentinel hybrid", render: renderSentinelHybrid },
};

const baseServices = [
  { name: "Home Hub", state: "healthy" },
  { name: "Media", state: "healthy" },
  { name: "Backups", state: "healthy" },
  { name: "DNS", state: "healthy" },
];

const fixtures = [
  makeFixture("healthy", "Healthy", "All monitored systems normal", "healthy"),
  makeFixture("warning", "Warning", "System disk is 82% full", "warning", {
    cpu: 48.2, memory: 71.4, disk: 82.1,
    reasons: ["System disk is 82% full"],
    services: baseServices.map((item, index) => index === 2 ? { ...item, state: "warning" } : item),
  }),
  makeFixture("critical", "Critical", "Home Hub failed two consecutive health checks", "critical", {
    cpu: 96.4, memory: 94.8, disk: 91.7,
    reasons: ["Home Hub failed two consecutive health checks", "System disk is 91% full", "CPU pressure persisted for 3 checks"],
    services: baseServices.map((item, index) => index === 0 ? { ...item, state: "critical" } : item),
  }),
  makeFixture("stale", "Data stale", "Last update is taking longer than expected", "stale", {
    age: "22s ago", ageShort: "22s", lastKnown: true,
  }),
  makeFixture("server-offline", "Server offline", "No server response for 2 minutes", "offline", {
    age: "2m ago", ageShort: "2m", lastKnown: true,
  }),
  makeFixture("wifi-offline", "Wi-Fi offline", "NOVA cannot reach the local network", "offline", {
    age: "8m ago", ageShort: "8m", lastKnown: true, failedLayer: "wifi",
  }),
  makeFixture("monitor-error", "Monitor error", "Device credentials were rejected", "error", {
    age: "14s ago", ageShort: "14s", lastKnown: true, failedLayer: "monitor",
    reasons: ["Device credentials were rejected"],
  }),
  makeFixture("setup", "Setup required", "Install Wi-Fi and server configuration, then restart", "setup", {
    cpu: null, memory: null, disk: null, uptime: null, age: "Never", ageShort: "—", services: [], failedLayer: "wifi",
  }),
  makeFixture("max-fresh", "Critical", "Monitoring integrity unavailable; last validated observation cannot be trusted safely", "critical", {
    cpu: 100, memory: 100, disk: 100,
    reasons: [
      "Monitoring integrity unavailable; last validated observation cannot be trusted safely",
      "Primary application endpoint failed consecutive checks and requires operator attention",
      "System disk capacity crossed its configured critical threshold at one hundred percent",
    ],
    services: [
      { name: "Primary Application API", state: "critical" },
      { name: "Automated Backup Service", state: "warning" },
      { name: "Local Domain Resolver", state: "unknown" },
      { name: "Household Automation", state: "healthy" },
    ],
  }),
  makeFixture("max-offline", "Server offline", "No accepted server snapshot for fifty-nine minutes", "offline", {
    cpu: 100, memory: 100, disk: 100, age: "59m ago", ageShort: "59m", lastKnown: true, failedLayer: "server",
    retainedTitle: "Critical",
    retainedSummary: "Monitoring integrity unavailable; last validated observation cannot be trusted safely",
    reasons: [
      "Monitoring integrity unavailable; last validated observation cannot be trusted safely",
      "Primary application endpoint failed consecutive checks and requires operator attention",
      "System disk capacity crossed its configured critical threshold at one hundred percent",
    ],
    services: [
      { name: "Primary Application API", state: "critical" },
      { name: "Automated Backup Service", state: "warning" },
      { name: "Local Domain Resolver", state: "unknown" },
      { name: "Household Automation", state: "healthy" },
    ],
  }),
  makeFixture("nulls", "Warning", "CPU and memory observations are unavailable", "warning", {
    cpu: null, memory: null, disk: 61.2, uptime: null,
    reasons: ["CPU and memory observations are unavailable"],
    services: baseServices.map((item, index) => index === 1 ? { ...item, state: "unknown" } : item),
  }),
];

function makeFixture(id, title, summary, tone, overrides = {}) {
  return {
    id, title, summary, tone,
    cpu: 24.1, memory: 61.0, disk: 54.3,
    uptime: "13d 9h", age: "2s ago", ageShort: "2s",
    lastKnown: false, failedLayer: id === "server-offline" ? "server" : null,
    reasons: [], services: baseServices,
    ...overrides,
  };
}

const params = new URLSearchParams(window.location.search);
let currentVariant = variants[params.get("variant")] ? params.get("variant") : "D";
let fixtureIndex = Math.max(0, fixtures.findIndex((fixture) => fixture.id === params.get("fixture")));
let currentPage = params.get("page") === "details" ? "details" : "home";

const device = document.querySelector("#device");
const variantLabel = document.querySelector("#variant-label");
const fixtureSelect = document.querySelector("#fixture-select");
const pageToggle = document.querySelector("#page-toggle");

fixtures.forEach((fixture, index) => {
  const option = document.createElement("option");
  option.value = String(index);
  option.textContent = fixture.id === "max-fresh" ? "Max strings · fresh" : fixture.id === "max-offline" ? "Max strings · offline" : fixture.title;
  fixtureSelect.append(option);
});

function render() {
  const fixture = fixtures[fixtureIndex];
  device.className = `device tone-${fixture.tone} variant-${currentVariant.toLowerCase()}`;
  device.innerHTML = variants[currentVariant].render(fixture, currentPage);
  variantLabel.textContent = `${currentVariant} — ${variants[currentVariant].name}`;
  fixtureSelect.value = String(fixtureIndex);
  pageToggle.textContent = currentPage === "home" ? "Details" : "Home";
  pageToggle.setAttribute("aria-label", `Open ${pageToggle.textContent}`);
  syncUrl();

  device.querySelectorAll("[data-page]").forEach((button) => {
    button.addEventListener("click", () => {
      currentPage = button.dataset.page;
      render();
    });
  });
}

function syncUrl() {
  const next = new URLSearchParams();
  next.set("variant", currentVariant);
  next.set("fixture", fixtures[fixtureIndex].id);
  if (currentPage === "details") next.set("page", "details");
  history.replaceState(null, "", `?${next}`);
}

function cycleVariant(delta) {
  const keys = Object.keys(variants);
  currentVariant = keys[(keys.indexOf(currentVariant) + delta + keys.length) % keys.length];
  render();
}

function cycleFixture(delta) {
  fixtureIndex = (fixtureIndex + delta + fixtures.length) % fixtures.length;
  render();
}

document.querySelector("#previous-variant").addEventListener("click", () => cycleVariant(-1));
document.querySelector("#next-variant").addEventListener("click", () => cycleVariant(1));
document.querySelector("#previous-fixture").addEventListener("click", () => cycleFixture(-1));
document.querySelector("#next-fixture").addEventListener("click", () => cycleFixture(1));
fixtureSelect.addEventListener("change", () => { fixtureIndex = Number(fixtureSelect.value); render(); });
pageToggle.addEventListener("click", () => { currentPage = currentPage === "home" ? "details" : "home"; render(); });
window.addEventListener("keydown", (event) => {
  if (["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement?.tagName) || document.activeElement?.isContentEditable) return;
  if (event.key === "ArrowLeft") cycleVariant(-1);
  if (event.key === "ArrowRight") cycleVariant(1);
  if (event.key === "ArrowUp") cycleFixture(-1);
  if (event.key === "ArrowDown") cycleFixture(1);
});

function stateGlyph(fixture) {
  const glyphs = { healthy: "✓", warning: "!", critical: "!", stale: "↻", offline: "×", error: "!", setup: "+" };
  return glyphs[fixture.tone];
}

function metricValue(value) {
  return value == null ? "Unavailable" : `${value.toFixed(1)}%`;
}

function metricShort(value) {
  return value == null ? "—" : `${Math.round(value)}%`;
}

function metricShortAscii(value) {
  return value == null ? "Unavailable" : `${Math.round(value)}%`;
}

function serviceStateGlyph(state) {
  return { healthy: "✓", warning: "!", critical: "×", unknown: "?" }[state];
}

function metricRow(label, value) {
  const width = value == null ? 0 : Math.max(2, Math.min(100, value));
  return `<div class="metric-row ${value == null ? "is-null" : ""}">
    <div class="metric-label"><span>${label}</span><strong>${metricValue(value)}</strong></div>
    <div class="metric-track"><i style="width:${width}%"></i></div>
  </div>`;
}

function metricRowAscii(label, value) {
  const width = value == null ? 0 : Math.max(2, Math.min(100, value));
  return `<div class="metric-row ${value == null ? "is-null" : ""}">
    <div class="metric-label"><span>${label}</span><strong>${value == null ? "Unavailable" : `${value.toFixed(1)}%`}</strong></div>
    <div class="metric-track"><i style="width:${width}%"></i></div>
  </div>`;
}

function serviceRows(services, compact = false) {
  if (!services.length) return `<div class="empty-services">No service checks configured</div>`;
  return services.map((service) => `<div class="service-row state-${service.state}">
    <span class="service-indicator" aria-hidden="true"></span>
    <span class="service-name">${service.name}</span>
    <span class="service-state">${compact ? service.state.slice(0, 1).toUpperCase() : service.state}</span>
  </div>`).join("");
}

function serviceGrid(services, asciiSymbols = false) {
  if (!services.length) return `<div class="empty-services">No service checks configured</div>`;
  return services.map((service) => `<div class="service-tile state-${service.state}" title="${service.name}: ${service.state}">
    <i>${asciiSymbols ? { healthy: "OK", warning: "!", critical: "X", unknown: "-" }[service.state] : serviceStateGlyph(service.state)}</i><span>${service.name}</span>
  </div>`).join("");
}

function scrollCue() {
  return `<div class="scroll-cue" aria-hidden="true">MORE v</div>`;
}

function connectionLayers(fixture) {
  if (fixture.id === "setup") return [
    { id: "wifi", label: "Wi-Fi", meta: "Not configured", state: "neutral" },
    { id: "server", label: "Server", meta: "Not configured", state: "neutral" },
    { id: "monitor", label: "Monitor", meta: "Not configured", state: "neutral" },
  ];
  if (fixture.id === "wifi-offline") return [
    { id: "wifi", label: "Wi-Fi", meta: "Disconnected", state: "failed" },
    { id: "server", label: "Server", meta: "Not evaluated", state: "blocked" },
    { id: "monitor", label: "Monitor", meta: "Not evaluated", state: "blocked" },
  ];
  if (fixture.id === "server-offline" || fixture.id === "max-offline") return [
    { id: "wifi", label: "Wi-Fi", meta: "Connected", state: "ok" },
    { id: "server", label: "Server", meta: "No response", state: "failed" },
    { id: "monitor", label: "Monitor", meta: "Not evaluated", state: "blocked" },
  ];
  if (fixture.id === "stale") return [
    { id: "wifi", label: "Wi-Fi", meta: "Connected", state: "ok" },
    { id: "server", label: "Server", meta: "Unknown", state: "unknown" },
    { id: "monitor", label: "Monitor", meta: "Not evaluated", state: "unknown" },
  ];
  if (fixture.id === "monitor-error") return [
    { id: "wifi", label: "Wi-Fi", meta: "Connected", state: "ok" },
    { id: "server", label: "Server", meta: "Unknown", state: "unknown" },
    { id: "monitor", label: "Monitor", meta: "Not evaluated", state: "unknown" },
  ];
  return [
    { id: "wifi", label: "Wi-Fi", meta: "Connected", state: "ok" },
    { id: "server", label: "Server", meta: "Reachable", state: "ok" },
    { id: "monitor", label: "Monitor", meta: "Validated", state: "ok" },
  ];
}

function header(fixture, brand = "NOVA") {
  const freshness = fixture.id === "setup" ? "NOT CONFIGURED" : `${fixture.lastKnown ? "LAST KNOWN · " : "LIVE · "}${fixture.age}`;
  return `<header class="topbar"><span class="brand">${brand}</span><span class="freshness ${fixture.lastKnown ? "last-known" : ""}">${freshness}</span></header>`;
}

function hybridHeader(fixture) {
  const freshness = fixture.id === "setup" ? "NOT CONFIGURED" : `${fixture.lastKnown ? "LAST KNOWN | " : "LIVE | "}${fixture.age}`;
  return `<header class="topbar"><span class="brand">NOVA</span><span class="freshness ${fixture.lastKnown ? "last-known" : ""}">${freshness}</span></header>`;
}

function hybridGlyph(fixture) {
  if (fixture.tone === "stale") return `<span class="stale-mark" aria-label="stale"></span>`;
  return { healthy: "OK", warning: "!", critical: "X", offline: "X", error: "!", setup: "+" }[fixture.tone];
}

function navButton(page, label, className = "screen-nav") {
  return `<button class="${className}" data-page="${page}">${label}</button>`;
}

function renderBeacon(fixture, page) {
  if (page === "details") return `${header(fixture)}
    <section class="a-details">
      <div class="section-title"><span>DETAILS</span><strong>${fixture.title}</strong></div>
      <div class="details-scroll">
        <p class="detail-summary">${fixture.summary}</p>
        <div class="fact-pair"><span>Server uptime</span><strong>${fixture.uptime ?? "Unavailable"}</strong></div>
        <div class="fact-pair"><span>Last accepted</span><strong>${fixture.age}</strong></div>
        <h2>Reasons</h2>
        ${fixture.reasons.length ? fixture.reasons.map((reason, i) => `<div class="reason"><b>${i + 1}</b><span>${reason}</span></div>`).join("") : `<p class="quiet-copy">No active reasons.</p>`}
        <h2>Service checks</h2>${serviceRows(fixture.services)}
      </div>
    </section>${scrollCue()}${navButton("home", "← Back")}`;

  return `${header(fixture)}
    <section class="beacon-hero">
      <div class="beacon-glyph">${stateGlyph(fixture)}</div>
      <div class="eyebrow">SYSTEM STATUS</div>
      <h1>${fixture.title}</h1>
      <p>${fixture.summary}</p>
    </section>
    <section class="beacon-metrics">
      ${metricRow("CPU", fixture.cpu)}${metricRow("Memory", fixture.memory)}${metricRow("Disk", fixture.disk)}
    </section>
    <section class="beacon-services"><div class="mini-title"><span>SERVICES</span><span>${fixture.lastKnown ? "LAST KNOWN" : `${fixture.services.filter(s => s.state === "healthy").length}/${fixture.services.length || "—"} OK`}</span></div>
      <div class="service-grid">${serviceGrid(fixture.services)}</div>
    </section>${navButton("details", "Details  →")}`;
}

function renderInstrument(fixture, page) {
  const severity = `<aside class="severity-rail"><span class="rail-word">${fixture.title}</span><b>${stateGlyph(fixture)}</b></aside>`;
  if (page === "details") return `${severity}<div class="instrument-body">${header(fixture, "NOVA / 01")}
    <section class="instrument-details">
      <div class="instrument-heading"><small>DIAGNOSTIC</small><h1>${fixture.title}</h1></div>
      <p class="instrument-summary">${fixture.summary}</p>
      <div class="instrument-facts"><div><span>UPTIME</span><b>${fixture.uptime ?? "Unavailable"}</b></div><div><span>SNAPSHOT</span><b>${fixture.age}</b></div></div>
      <h2>REASONS</h2>
      <div class="reason-stack">${fixture.reasons.length ? fixture.reasons.map(reason => `<p>${reason}</p>`).join("") : `<p class="quiet-copy">No active reasons</p>`}</div>
      <h2>CHECKS</h2><div class="instrument-service-list">${serviceRows(fixture.services)}</div>
    </section>${scrollCue()}${navButton("home", "BACK", "instrument-nav")}</div>`;

  return `${severity}<div class="instrument-body">${header(fixture, "NOVA / 01")}
    <section class="instrument-status"><div class="eyebrow">CURRENT CONDITION</div><h1>${fixture.title}</h1><p>${fixture.summary}</p></section>
    <section class="instrument-metrics">
      ${[["CPU", fixture.cpu], ["MEM", fixture.memory], ["DSK", fixture.disk]].map(([label, value]) => `<div class="metric-cell ${value == null ? "is-null" : ""}"><span>${label}</span><strong>${metricShort(value)}</strong><i><b style="height:${value == null ? 0 : value}%"></b></i></div>`).join("")}
    </section>
      <section class="instrument-services"><div class="instrument-caption"><span>CHECK MATRIX</span><span>UP ${fixture.uptime ?? "—"}</span></div><div class="check-matrix">${fixture.services.length ? fixture.services.map((s, i) => `<div class="matrix-cell state-${s.state}"><span>0${i + 1}</span><b>${s.name}</b><i aria-label="${s.state}">${serviceStateGlyph(s.state)}</i></div>`).join("") : `<div class="empty-services">No checks</div>`}</div></section>
    ${navButton("details", "OPEN DETAILS", "instrument-nav")}</div>`;
}

function renderSignalPath(fixture, page) {
  const layers = connectionLayers(fixture);
  if (page === "details") return `${header(fixture, "NOVA SIGNAL")}
    <section class="signal-detail-head"><span class="signal-mark">${stateGlyph(fixture)}</span><div><small>DIAGNOSTIC REPORT</small><h1>${fixture.title}</h1></div></section>
    <section class="signal-detail-scroll">
      <p class="signal-summary">${fixture.summary}</p>
      <div class="signal-facts"><span>UP ${fixture.uptime ?? "Unavailable"}</span><span>AGE ${fixture.age}</span></div>
      <h2>Connection path</h2><div class="mini-path">${layers.map(layer => `<span class="${layer.state}">${layer.label}: ${layer.meta}</span>`).join("<i>›</i>")}</div>
      <h2>Reasons</h2>${fixture.reasons.length ? fixture.reasons.map(r => `<p class="signal-reason">${r}</p>`).join("") : `<p class="quiet-copy">No active reasons.</p>`}
      <h2>Services</h2>${serviceRows(fixture.services)}
    </section>${scrollCue()}${navButton("home", "Back to status")}`;

  return `${header(fixture, "NOVA SIGNAL")}
    <section class="signal-head"><div class="signal-mark">${stateGlyph(fixture)}</div><div class="signal-copy"><div class="eyebrow">SERVER SENTINEL</div><h1>${fixture.title}</h1><p>${fixture.summary}</p></div></section>
    <section class="path-list">${layers.map((layer, index) => {
      return `<div class="path-node ${layer.state}"><span class="node-number">0${index + 1}</span><i></i><div><b>${layer.label}</b><small>${layer.meta}</small></div><strong>${layer.state === "failed" ? "×" : layer.state === "ok" ? "✓" : "—"}</strong></div>`;
    }).join("")}</section>
    <section class="signal-metrics">${[["CPU", fixture.cpu], ["MEM", fixture.memory], ["DISK", fixture.disk]].map(([l,v]) => `<div><span>${l}</span><b>${metricShort(v)}</b></div>`).join("")}</section>
    <section class="signal-service-line"><span>SERVICES</span>${fixture.services.length ? fixture.services.map((s) => `<i class="state-${s.state}" title="${s.name}: ${s.state}">${serviceStateGlyph(s.state)}</i>`).join("") : `<b>NONE</b>`}<strong>${fixture.ageShort}</strong></section>
    ${navButton("details", "Inspect details")}`;
}

function lastKnownBand(fixture) {
  return fixture.lastKnown ? `<div class="last-known-band"><b>LAST KNOWN DATA</b><span>Accepted ${fixture.age}</span></div>` : "";
}

function hybridPath(layers) {
  return `<section class="hybrid-path">${layers.map((layer, index) => `<div class="hybrid-node ${layer.state}">
    <i></i><div><b>${layer.label}</b><small>${layer.meta}</small></div><strong>${layer.state === "ok" ? "OK" : layer.state === "failed" ? "X" : "-"}</strong>
  </div>`).join("")}</section>`;
}

function renderSentinelHybrid(fixture, page) {
  const layers = connectionLayers(fixture);
  const diagnostic = ["stale", "server-offline", "wifi-offline", "monitor-error", "setup", "max-offline"].includes(fixture.id);
  const serviceCountLabel = fixture.lastKnown ? "LAST KNOWN" : fixture.services.length ? `${fixture.services.filter(s => s.state === "healthy").length}/${fixture.services.length} OK` : "NO CHECKS";
  if (page === "details") {
    return `${hybridHeader(fixture)}${lastKnownBand(fixture)}
      <section class="hybrid-details ${fixture.lastKnown ? "with-band" : ""}">
        <div class="hybrid-detail-title"><span class="hybrid-small-glyph">${hybridGlyph(fixture)}</span><div><small>CURRENT STATE</small><h1>${fixture.title}</h1></div></div>
        <p class="hybrid-current-summary">${fixture.summary}</p>
        ${diagnostic ? `<h2>Connection path</h2>${hybridPath(layers)}` : ""}
        ${fixture.lastKnown ? `<div class="retained-heading"><span>RETAINED SNAPSHOT</span><b>${fixture.retainedTitle ?? "Last accepted"}</b></div>${fixture.retainedSummary ? `<p class="retained-summary">${fixture.retainedSummary}</p>` : ""}` : ""}
        <div class="hybrid-facts ${fixture.lastKnown ? "retained-data" : ""}"><div><span>UPTIME</span><b>${fixture.uptime ?? "Unavailable"}</b></div><div><span>ACCEPTED</span><b>${fixture.age}</b></div></div>
        <h2>${fixture.lastKnown ? "Last-known metrics" : "Metrics"}</h2>
        <div class="hybrid-detail-metrics ${fixture.lastKnown ? "retained-data" : ""}">${metricRowAscii("CPU", fixture.cpu)}${metricRowAscii("Memory", fixture.memory)}${metricRowAscii("Disk", fixture.disk)}</div>
        <h2>${fixture.lastKnown ? "Last-known reasons" : "Reasons"}</h2>
        <div class="hybrid-reasons ${fixture.lastKnown ? "retained-data" : ""}">${fixture.reasons.length ? fixture.reasons.map((reason, index) => `<div><b>${index + 1}</b><p>${reason}</p></div>`).join("") : `<p class="quiet-copy">No active reasons.</p>`}</div>
        <h2>${fixture.lastKnown ? "Last-known services" : "Services"}</h2>
        <div class="hybrid-detail-services ${fixture.lastKnown ? "retained-data" : ""}">${serviceRows(fixture.services)}</div>
        <div class="scroll-end">END OF DETAILS</div>
      </section>${scrollCue()}${navButton("home", "< Back")}`;
  }

  return `${hybridHeader(fixture)}${lastKnownBand(fixture)}
    <section class="hybrid-hero ${fixture.lastKnown ? "compact" : ""}">
      <div class="hybrid-glyph">${hybridGlyph(fixture)}</div><div><small>SYSTEM STATUS</small><h1>${fixture.title}</h1><p>${fixture.summary}</p></div>
    </section>
    ${diagnostic ? hybridPath(layers) : `<section class="hybrid-metrics">${metricRowAscii("CPU", fixture.cpu)}${metricRowAscii("Memory", fixture.memory)}${metricRowAscii("Disk", fixture.disk)}</section>`}
    <section class="hybrid-retained ${fixture.lastKnown ? "retained-data" : ""}">
      ${diagnostic ? `<div class="compact-metrics">${[["CPU",fixture.cpu],["MEM",fixture.memory],["DISK",fixture.disk]].map(([l,v]) => `<span><small>${l}</small><b>${metricShortAscii(v)}</b></span>`).join("")}</div>` : ""}
      <div class="hybrid-service-title"><span>SERVICES</span><b>${serviceCountLabel}</b></div>
      <div class="service-grid">${serviceGrid(fixture.services, true)}</div>
    </section>${navButton("details", "Details >")}`;
}

render();
