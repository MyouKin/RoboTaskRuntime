const $ = (id) => document.getElementById(id);

const STORAGE_KEY = "rtr.dashboard.v2";
const REFRESH_MS = 500;
const CHART_COLORS = [
  "#67d7a5",
  "#7aa8ff",
  "#f2c45d",
  "#f06d6d",
  "#63d6e8",
  "#d69bf7",
  "#9ee493",
  "#ff9f7a",
  "#b9a7ff",
  "#7be0c3",
];

const PANEL_TYPES = {
  system: { label: "System", minW: 520, minH: 160 },
  chart: { label: "Debug Curves", minW: 460, minH: 330 },
  image: { label: "Debug Image", minW: 360, minH: 360 },
  params: { label: "Parameters", minW: 620, minH: 320 },
  values: { label: "Debug Values", minW: 420, minH: 300 },
  logs: { label: "Logs", minW: 420, minH: 260 },
};

const SYSTEM_FIELDS = [
  ["runtime", "Runtime"],
  ["task", "Task"],
  ["loop_hz", "Loop Hz"],
  ["update_ms", "Update ms"],
  ["frame", "Frame"],
  ["webui", "WebUI"],
];

const state = {
  panels: [],
  params: [],
  status: {},
  logs: [],
  debug: { images: [], values: [], history: {} },
  editingKey: null,
  dragging: null,
  resizing: null,
  initialized: false,
};

function defaultPanels() {
  return [
    panel("system", 16, 16, 1160, 150),
    panel("chart", 16, 184, 690, 420, {
      selectedKeys: ["spr.command.yaw_deg", "spr.command.pitch_deg"],
    }),
    panel("image", 724, 184, 452, 420, { imageKey: "spr.fake.overlay" }),
    panel("params", 16, 622, 1160, 380),
    panel("values", 16, 1020, 560, 330),
    panel("logs", 594, 1020, 582, 330),
  ];
}

function panel(type, x, y, w, h, config = {}) {
  return {
    id: `${type}-${Date.now()}-${Math.random().toString(16).slice(2)}`,
    type,
    x,
    y,
    w,
    h,
    configOpen: false,
    config,
  };
}

function loadPanels() {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || "null");
    if (Array.isArray(parsed?.panels) && parsed.panels.length) {
      state.panels = parsed.panels.filter((p) => PANEL_TYPES[p.type]);
    }
  } catch (error) {
    console.warn(error);
  }
  if (!state.panels.length) {
    state.panels = defaultPanels();
  }
}

function savePanels() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify({ panels: state.panels }));
}

async function api(path, options = {}) {
  const response = await fetch(path, options);
  if (!response.ok) {
    throw new Error(await response.text());
  }
  return response.json();
}

function postCommand(command) {
  api(`/api/${command}`, { method: "POST" }).catch(console.error);
}

function formatValue(value) {
  if (typeof value === "number") {
    if (!Number.isFinite(value)) return "--";
    const abs = Math.abs(value);
    if (abs >= 1000) return value.toFixed(0);
    if (abs >= 100) return value.toFixed(1);
    if (abs >= 10) return value.toFixed(2);
    if (abs >= 1) return value.toFixed(3);
    return value.toPrecision(4);
  }
  if (typeof value === "boolean") return value ? "true" : "false";
  return String(value ?? "--");
}

function setText(id, value) {
  const element = $(id);
  if (element) element.textContent = value;
}

function colorForKey(key, index) {
  let hash = 0;
  for (let i = 0; i < key.length; i += 1) {
    hash = (hash * 31 + key.charCodeAt(i)) >>> 0;
  }
  return CHART_COLORS[(hash + index) % CHART_COLORS.length];
}

function numericHistoryKeys() {
  return Object.keys(state.debug.history ?? {})
    .filter((key) => Array.isArray(state.debug.history[key]) &&
      state.debug.history[key].length >= 2)
    .sort();
}

function defaultChartKeys() {
  const keys = numericHistoryKeys();
  const preferred = [
    ["spr.command.yaw_deg", "spr.command.pitch_deg"],
    ["command.yaw", "command.pitch"],
    ["spr.plan.yaw", "spr.plan.pitch"],
  ];
  for (const group of preferred) {
    const picked = group.filter((key) => keys.includes(key));
    if (picked.length) return picked;
  }
  return keys.slice(0, 6);
}

function seriesForPanel(panelConfig) {
  const configured = (panelConfig.selectedKeys ?? []).filter(
    (key) => Array.isArray(state.debug.history?.[key]) &&
      state.debug.history[key].length >= 2,
  );
  const keys = configured.length ? configured : defaultChartKeys();
  return keys.map((key, index) => ({
    key,
    color: colorForKey(key, index),
    points: state.debug.history[key] ?? [],
  }));
}

function addPanel(type) {
  const count = state.panels.length;
  const def = PANEL_TYPES[type];
  const created = panel(
    type,
    24 + (count % 4) * 34,
    80 + (count % 6) * 38,
    Math.max(def.minW, type === "system" || type === "params" ? 760 : def.minW),
    def.minH + (type === "chart" ? 90 : 30),
  );
  state.panels.push(created);
  savePanels();
  renderPanelShells();
}

function duplicatePanel(source) {
  state.panels.push({
    ...source,
    id: `${source.type}-${Date.now()}-${Math.random().toString(16).slice(2)}`,
    x: source.x + 32,
    y: source.y + 32,
    config: { ...source.config },
  });
  savePanels();
  renderPanelShells();
}

function removePanel(id) {
  state.panels = state.panels.filter((panelItem) => panelItem.id !== id);
  savePanels();
  renderPanelShells();
}

function updateDashboardHeight() {
  const bottom = state.panels.reduce((max, item) => Math.max(max, item.y + item.h), 0);
  $("dashboard").style.minHeight = `${Math.max(720, bottom + 32)}px`;
}

function renderPanelShells() {
  const host = $("dashboard");
  host.innerHTML = "";
  updateDashboardHeight();

  for (const item of state.panels) {
    const meta = PANEL_TYPES[item.type];
    const section = document.createElement("section");
    section.className = `panel dashboard-panel ${item.type}-panel`;
    section.dataset.panelId = item.id;
    section.style.left = `${item.x}px`;
    section.style.top = `${item.y}px`;
    section.style.width = `${item.w}px`;
    section.style.height = `${item.h}px`;

    const heading = document.createElement("div");
    heading.className = "panel-heading drag-handle";
    heading.addEventListener("pointerdown", (event) => startDrag(event, item.id));
    heading.addEventListener("mousedown", (event) => startDrag(event, item.id));

    const title = document.createElement("h2");
    title.textContent = meta.label;
    heading.appendChild(title);

    const actions = document.createElement("div");
    actions.className = "panel-actions";
    actions.append(
      panelButton("Cfg", () => {
        item.configOpen = !item.configOpen;
        savePanels();
        renderPanelShells();
      }),
      panelButton("Copy", () => duplicatePanel(item)),
      panelButton("Close", () => removePanel(item.id), "danger"),
    );
    heading.appendChild(actions);

    const config = document.createElement("div");
    config.className = `panel-config ${item.configOpen ? "open" : ""}`;
    config.dataset.configFor = item.id;

    const content = document.createElement("div");
    content.className = "panel-content";
    content.dataset.contentFor = item.id;

    const resizer = document.createElement("div");
    resizer.className = "panel-resizer";
    resizer.addEventListener("pointerdown", (event) => startResize(event, item.id));
    resizer.addEventListener("mousedown", (event) => startResize(event, item.id));

    section.append(heading, config, content, resizer);
    host.appendChild(section);
  }

  updateAllPanels();
}

function panelButton(label, onClick, tone = "") {
  const button = document.createElement("button");
  button.type = "button";
  button.className = `small-button ${tone}`;
  button.textContent = label;
  button.addEventListener("click", (event) => {
    event.stopPropagation();
    onClick();
  });
  return button;
}

function startDrag(event, id) {
  if (event.type === "mousedown" && state.dragging) return;
  if (event.target.closest("button, input, select, label")) return;
  const item = state.panels.find((panelItem) => panelItem.id === id);
  if (!item) return;
  const rect = event.currentTarget.closest(".dashboard-panel").getBoundingClientRect();
  state.dragging = {
    id,
    dx: event.clientX - rect.left,
    dy: event.clientY - rect.top,
  };
  event.preventDefault();
  if (event.currentTarget.setPointerCapture && event.pointerId !== undefined) {
    event.currentTarget.setPointerCapture(event.pointerId);
  }
}

function startResize(event, id) {
  if (event.type === "mousedown" && state.resizing) return;
  event.stopPropagation();
  const item = state.panels.find((panelItem) => panelItem.id === id);
  if (!item) return;
  state.resizing = {
    id,
    startX: event.clientX,
    startY: event.clientY,
    w: item.w,
    h: item.h,
  };
  event.preventDefault();
  if (event.currentTarget.setPointerCapture && event.pointerId !== undefined) {
    event.currentTarget.setPointerCapture(event.pointerId);
  }
}

function applyPointerMove(event) {
  const dashboardRect = $("dashboard").getBoundingClientRect();
  if (state.dragging) {
    const item = state.panels.find((panelItem) => panelItem.id === state.dragging.id);
    if (!item) return;
    item.x = Math.max(0, event.clientX - dashboardRect.left - state.dragging.dx);
    item.y = Math.max(0, event.clientY - dashboardRect.top - state.dragging.dy);
    const element = document.querySelector(`[data-panel-id="${CSS.escape(item.id)}"]`);
    if (element) {
      element.style.left = `${item.x}px`;
      element.style.top = `${item.y}px`;
    }
    updateDashboardHeight();
  }
  if (state.resizing) {
    const item = state.panels.find((panelItem) => panelItem.id === state.resizing.id);
    if (!item) return;
    const meta = PANEL_TYPES[item.type];
    item.w = Math.max(meta.minW, state.resizing.w + event.clientX - state.resizing.startX);
    item.h = Math.max(meta.minH, state.resizing.h + event.clientY - state.resizing.startY);
    const element = document.querySelector(`[data-panel-id="${CSS.escape(item.id)}"]`);
    if (element) {
      element.style.width = `${item.w}px`;
      element.style.height = `${item.h}px`;
    }
    updateDashboardHeight();
    updatePanelContent(item);
  }
}

function finishPointerChange() {
  if (state.dragging || state.resizing) {
    state.dragging = null;
    state.resizing = null;
    savePanels();
  }
}

function updateAllPanels() {
  setText("statusBadge", state.status.runtime_status ?? "Disconnected");
  for (const item of state.panels) {
    updatePanelConfig(item);
    updatePanelContent(item);
  }
}

function updatePanelConfig(item) {
  const box = document.querySelector(`[data-config-for="${CSS.escape(item.id)}"]`);
  if (!box || !item.configOpen) {
    if (box) box.innerHTML = "";
    return;
  }
  if (item.type === "system") {
    renderSystemConfig(item, box);
  } else if (item.type === "chart") {
    renderChartConfig(item, box);
  } else if (item.type === "image") {
    renderImageConfig(item, box);
  } else if (item.type === "params" || item.type === "values") {
    renderFilterConfig(item, box);
  } else if (item.type === "logs") {
    renderLogsConfig(item, box);
  }
}

function updatePanelContent(item) {
  const content = document.querySelector(`[data-content-for="${CSS.escape(item.id)}"]`);
  if (!content) return;
  if (item.type === "system") renderSystemPanel(item, content);
  if (item.type === "chart") renderChartPanel(item, content);
  if (item.type === "image") renderImagePanel(item, content);
  if (item.type === "params") renderParamsPanel(item, content);
  if (item.type === "values") renderValuesPanel(item, content);
  if (item.type === "logs") renderLogsPanel(item, content);
}

function renderSystemPanel(item, content) {
  const status = state.status;
  const selected = new Set(item.config.fields ?? SYSTEM_FIELDS.map((field) => field[0]));
  content.innerHTML = "";
  const grid = document.createElement("div");
  grid.className = "system-grid";
  const values = {
    runtime: status.runtime_status ?? "--",
    task: status.task_name ?? "--",
    loop_hz: formatValue(status.loop_hz),
    update_ms: formatValue(status.update_ms),
    frame: String(status.frame_index ?? "--"),
    webui: status.webui ? "On" : "Off",
  };
  SYSTEM_FIELDS.filter(([key]) => selected.has(key)).forEach(([key, label]) => {
    const item = document.createElement("div");
    item.className = "system-metric";
    const labelElement = document.createElement("span");
    labelElement.textContent = label;
    const strong = document.createElement("strong");
    strong.textContent = values[key];
    item.append(labelElement, strong);
    grid.appendChild(item);
  });
  content.appendChild(grid);
}

function renderSystemConfig(item, box) {
  const selected = new Set(item.config.fields ?? SYSTEM_FIELDS.map((field) => field[0]));
  box.innerHTML = "";
  const list = document.createElement("div");
  list.className = "check-list compact";
  for (const [key, labelText] of SYSTEM_FIELDS) {
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = selected.has(key);
    input.addEventListener("change", () => {
      const next = new Set(item.config.fields ?? SYSTEM_FIELDS.map((field) => field[0]));
      if (input.checked) next.add(key);
      else next.delete(key);
      item.config.fields = [...next];
      savePanels();
      updatePanelContent(item);
    });
    const text = document.createElement("span");
    text.textContent = labelText;
    label.append(input, text);
    list.appendChild(label);
  }
  box.appendChild(list);
}

function renderChartConfig(item, box) {
  const selected = new Set(item.config.selectedKeys ?? []);
  const keys = numericHistoryKeys();
  box.innerHTML = "";
  const list = document.createElement("div");
  list.className = "check-list";
  for (const key of keys) {
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = selected.has(key);
    input.addEventListener("change", () => {
      const next = new Set(item.config.selectedKeys ?? []);
      if (input.checked) next.add(key);
      else next.delete(key);
      item.config.selectedKeys = [...next].sort();
      savePanels();
      updatePanelContent(item);
    });
    const text = document.createElement("span");
    text.textContent = key;
    label.append(input, text);
    list.appendChild(label);
  }
  box.appendChild(list);
}

function renderImageConfig(item, box) {
  box.innerHTML = "";
  const select = document.createElement("select");
  select.className = "config-select";
  for (const key of state.debug.images ?? []) {
    const option = document.createElement("option");
    option.value = key;
    option.textContent = key;
    option.selected = key === item.config.imageKey;
    select.appendChild(option);
  }
  select.addEventListener("change", () => {
    item.config.imageKey = select.value;
    savePanels();
    updatePanelContent(item);
  });
  box.appendChild(select);
}

function renderFilterConfig(item, box) {
  box.innerHTML = "";
  const input = document.createElement("input");
  input.type = "text";
  input.placeholder = "filter";
  input.value = item.config.filter ?? "";
  input.addEventListener("input", () => {
    item.config.filter = input.value;
    savePanels();
    updatePanelContent(item);
  });
  box.appendChild(input);
}

function renderLogsConfig(item, box) {
  box.innerHTML = "";
  const input = document.createElement("input");
  input.type = "number";
  input.min = "10";
  input.max = "300";
  input.step = "10";
  input.value = item.config.limit ?? 80;
  input.addEventListener("change", () => {
    item.config.limit = Math.max(10, Number.parseInt(input.value || "80", 10));
    savePanels();
    updatePanelContent(item);
  });
  box.appendChild(input);
}

function renderChartPanel(item, content) {
  content.innerHTML = "";
  const series = seriesForPanel(item.config);
  const legend = document.createElement("div");
  legend.className = "chart-legend";
  for (const line of series) {
    const values = line.points.map((point) => point[1]);
    const min = Math.min(...values);
    const max = Math.max(...values);
    const latest = values[values.length - 1];
    const pill = document.createElement("div");
    pill.className = "legend-item";
    pill.innerHTML =
      `<span class="legend-dot" style="background:${line.color}"></span>` +
      `<span class="legend-key"></span>` +
      `<span class="legend-value">${formatValue(latest)}</span>` +
      `<span class="legend-range">${formatValue(min)} / ${formatValue(max)}</span>`;
    pill.querySelector(".legend-key").textContent = line.key;
    legend.appendChild(pill);
  }

  const wrap = document.createElement("div");
  wrap.className = "chart-wrap";
  const canvas = document.createElement("canvas");
  const tooltip = document.createElement("div");
  tooltip.className = "chart-tooltip";
  wrap.append(canvas, tooltip);
  content.append(legend, wrap);

  const draw = (hoverX = null) => {
    const hover = drawChart(canvas, series, hoverX);
    if (!hover) {
      tooltip.classList.remove("visible");
      return;
    }
    tooltip.classList.add("visible");
    tooltip.style.left = `${Math.min(hover.x + 12, canvas.clientWidth - 190)}px`;
    tooltip.style.top = `${Math.max(8, hover.y - 12)}px`;
    tooltip.innerHTML = hover.rows
      .map((row) => `<div><b style="color:${row.color}">${row.key}</b> ${formatValue(row.value)}</div>`)
      .join("");
  };

  canvas.addEventListener("mousemove", (event) => {
    const rect = canvas.getBoundingClientRect();
    draw(event.clientX - rect.left);
  });
  canvas.addEventListener("mouseleave", () => draw(null));
  draw(null);
}

function setupCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(260, Math.round(rect.width || canvas.clientWidth || 640));
  const height = Math.max(220, Math.round(rect.height || canvas.clientHeight || 280));
  const pixelWidth = Math.round(width * dpr);
  const pixelHeight = Math.round(height * dpr);
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, width, height };
}

function drawChart(canvas, series, hoverX) {
  const { ctx, width, height } = setupCanvas(canvas);
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b0d11";
  ctx.fillRect(0, 0, width, height);

  if (!series.length) {
    ctx.fillStyle = "#76808f";
    ctx.font = "13px ui-monospace, monospace";
    ctx.textAlign = "center";
    ctx.fillText("No scalar history", width / 2, height / 2);
    ctx.textAlign = "start";
    return null;
  }

  const margins = { left: 60, right: 20, top: 18, bottom: 34 };
  const plotX = margins.left;
  const plotY = margins.top;
  const plotW = Math.max(80, width - margins.left - margins.right);
  const plotH = Math.max(80, height - margins.top - margins.bottom);
  const allPoints = series.flatMap((item) => item.points);
  const minT = Math.min(...allPoints.map((point) => point[0]));
  const maxT = Math.max(...allPoints.map((point) => point[0]));
  let minY = Math.min(...allPoints.map((point) => point[1]));
  let maxY = Math.max(...allPoints.map((point) => point[1]));

  if (minY === maxY) {
    minY -= 1;
    maxY += 1;
  } else {
    const padding = (maxY - minY) * 0.12;
    minY -= padding;
    maxY += padding;
  }

  const xFor = (time) => plotX + ((time - minT) / Math.max(maxT - minT, 1)) * plotW;
  const yFor = (value) => plotY + ((maxY - value) / (maxY - minY)) * plotH;
  const timeForX = (x) => minT + ((x - plotX) / plotW) * Math.max(maxT - minT, 1);

  ctx.save();
  ctx.strokeStyle = "#27303b";
  ctx.fillStyle = "#8c96a5";
  ctx.font = "11px ui-monospace, monospace";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) {
    const ratio = i / 4;
    const y = plotY + ratio * plotH;
    const value = maxY - ratio * (maxY - minY);
    ctx.beginPath();
    ctx.moveTo(plotX, y);
    ctx.lineTo(plotX + plotW, y);
    ctx.stroke();
    ctx.fillText(formatValue(value), 8, y + 4);
  }
  for (let i = 0; i <= 4; i += 1) {
    const ratio = i / 4;
    const x = plotX + ratio * plotW;
    const time = minT + ratio * (maxT - minT);
    const secondsAgo = (maxT - time) / 1000;
    ctx.beginPath();
    ctx.moveTo(x, plotY);
    ctx.lineTo(x, plotY + plotH);
    ctx.stroke();
    ctx.fillText(`-${secondsAgo.toFixed(1)}s`, x - 18, plotY + plotH + 22);
  }
  if (minY < 0 && maxY > 0) {
    const zeroY = yFor(0);
    ctx.strokeStyle = "#566172";
    ctx.beginPath();
    ctx.moveTo(plotX, zeroY);
    ctx.lineTo(plotX + plotW, zeroY);
    ctx.stroke();
  }
  ctx.strokeStyle = "#596473";
  ctx.strokeRect(plotX, plotY, plotW, plotH);
  ctx.restore();

  for (const item of series) {
    ctx.save();
    ctx.beginPath();
    item.points.forEach((point, index) => {
      const x = xFor(point[0]);
      const y = yFor(point[1]);
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = item.color;
    ctx.lineWidth = 2.2;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.shadowColor = item.color;
    ctx.shadowBlur = 8;
    ctx.stroke();

    const last = item.points[item.points.length - 1];
    ctx.shadowBlur = 0;
    ctx.fillStyle = item.color;
    ctx.beginPath();
    ctx.arc(xFor(last[0]), yFor(last[1]), 3.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  if (hoverX === null || hoverX < plotX || hoverX > plotX + plotW) return null;
  const targetTime = timeForX(hoverX);
  const rows = [];
  let markerY = plotY + plotH / 2;
  ctx.save();
  ctx.strokeStyle = "#dfe5ee";
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(hoverX, plotY);
  ctx.lineTo(hoverX, plotY + plotH);
  ctx.stroke();
  ctx.setLineDash([]);
  for (const item of series) {
    const nearest = item.points.reduce((best, point) =>
      Math.abs(point[0] - targetTime) < Math.abs(best[0] - targetTime) ? point : best,
    item.points[0]);
    const x = xFor(nearest[0]);
    const y = yFor(nearest[1]);
    markerY = y;
    rows.push({ key: item.key, value: nearest[1], color: item.color });
    ctx.fillStyle = "#0b0d11";
    ctx.strokeStyle = item.color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  }
  ctx.restore();
  return { x: hoverX, y: markerY, rows };
}

function renderImagePanel(item, content) {
  content.innerHTML = "";
  const key = state.debug.images.includes(item.config.imageKey)
    ? item.config.imageKey
    : state.debug.images[0];
  if (key && item.config.imageKey !== key) {
    item.config.imageKey = key;
    savePanels();
  }
  const img = document.createElement("img");
  img.alt = key || "No debug image";
  if (key) {
    img.src = `/api/debug/image/${encodeURIComponent(key)}?t=${Date.now()}`;
  }
  content.appendChild(img);
}

function renderParamsPanel(item, content) {
  if (state.editingKey && content.dataset.rendered === "true") return;
  content.dataset.rendered = "true";
  const filter = (item.config.filter ?? "").toLowerCase();
  const params = state.params.filter((paramItem) => paramItem.key.toLowerCase().includes(filter));
  content.innerHTML = "";
  const wrap = document.createElement("div");
  wrap.className = "table-wrap";
  const table = document.createElement("table");
  table.innerHTML =
    "<thead><tr><th>Key</th><th>Value</th><th>Type</th><th></th></tr></thead><tbody></tbody>";
  const body = table.querySelector("tbody");
  for (const paramItem of params) {
    const tr = document.createElement("tr");
    const key = document.createElement("td");
    key.className = "key";
    key.textContent = paramItem.key;
    const value = document.createElement("td");
    const control = paramControl(paramItem);
    value.appendChild(control.input);
    const type = document.createElement("td");
    type.textContent = paramItem.type;
    const action = document.createElement("td");
    if (control.button) action.appendChild(control.button);
    tr.append(key, value, type, action);
    body.appendChild(tr);
  }
  wrap.appendChild(table);
  content.appendChild(wrap);
}

function paramControl(paramItem) {
  if (paramItem.type === "bool") {
    const select = document.createElement("select");
    select.dataset.key = paramItem.key;
    select.disabled = !paramItem.mutable;
    for (const value of ["true", "false"]) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = value;
      option.selected = String(paramItem.value) === value;
      select.appendChild(option);
    }
    select.addEventListener("change", () => setParam(paramItem.key, select.value));
    return { input: select, button: null };
  }

  const input = document.createElement("input");
  input.dataset.key = paramItem.key;
  input.disabled = !paramItem.mutable;
  input.value = String(paramItem.value ?? "");
  if (paramItem.type === "double" || paramItem.type === "int") {
    input.type = "number";
    input.step = paramItem.type === "int" ? "1" : "any";
    if (paramItem.min !== null && paramItem.min !== undefined) input.min = String(paramItem.min);
    if (paramItem.max !== null && paramItem.max !== undefined) input.max = String(paramItem.max);
    input.inputMode = paramItem.type === "int" ? "numeric" : "decimal";
  } else {
    input.type = "text";
  }
  input.addEventListener("focus", () => {
    state.editingKey = paramItem.key;
  });
  input.addEventListener("blur", () => {
    if (state.editingKey === paramItem.key) state.editingKey = null;
  });
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") setParam(paramItem.key, input.value);
  });

  const button = panelButton("Set", () => setParam(paramItem.key, input.value));
  button.disabled = !paramItem.mutable;
  return { input, button };
}

async function setParam(key, value) {
  await api("/api/params/set", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ key, value: String(value) }),
  }).catch((error) => console.error(error));
  state.editingKey = null;
}

function renderValuesPanel(item, content) {
  const filter = (item.config.filter ?? "").toLowerCase();
  const values = [...(state.debug.values ?? [])]
    .filter((entry) => entry.key.toLowerCase().includes(filter))
    .sort((a, b) => a.key.localeCompare(b.key));
  content.innerHTML = "";
  const grid = document.createElement("div");
  grid.className = "debug-values-grid";
  if (!values.length) {
    const empty = document.createElement("div");
    empty.className = "empty-debug";
    empty.textContent = "No debug values";
    grid.appendChild(empty);
  }
  for (const entry of values) {
    const div = document.createElement("div");
    div.className = `debug-value-item ${entry.type}`;
    const key = document.createElement("span");
    key.textContent = entry.key;
    const value = document.createElement("strong");
    value.textContent = formatValue(entry.value);
    div.append(key, value);
    grid.appendChild(div);
  }
  content.appendChild(grid);
}

function renderLogsPanel(item, content) {
  const limit = item.config.limit ?? 80;
  content.innerHTML = "";
  const box = document.createElement("div");
  box.className = "logs";
  for (const entry of state.logs.slice(-limit).reverse()) {
    const line = document.createElement("div");
    line.className = `log-line ${entry.level.toLowerCase()}`;
    const level = document.createElement("b");
    level.textContent = entry.level;
    const msg = document.createElement("span");
    msg.textContent = entry.message;
    line.append(level, msg);
    box.appendChild(line);
  }
  content.appendChild(box);
}

async function refresh() {
  try {
    const [status, params, logs, debug] = await Promise.all([
      api("/api/runtime/status"),
      api("/api/params"),
      api("/api/logs"),
      api("/api/debug/list"),
    ]);
    state.status = status;
    state.params = params.params ?? [];
    state.logs = logs.logs ?? [];
    state.debug = debug ?? { images: [], values: [], history: {} };
    updateAllPanels();
  } catch (error) {
    setText("statusBadge", "Disconnected");
  }
}

for (const button of document.querySelectorAll("[data-command]")) {
  button.addEventListener("click", () => postCommand(button.dataset.command));
}

$("addPanel").addEventListener("click", () => addPanel($("panelType").value));
$("resetLayout").addEventListener("click", () => {
  state.panels = defaultPanels();
  savePanels();
  renderPanelShells();
});

document.addEventListener("pointermove", applyPointerMove);
document.addEventListener("pointerup", finishPointerChange);
document.addEventListener("mousemove", applyPointerMove);
document.addEventListener("mouseup", finishPointerChange);
window.addEventListener("resize", () => updateAllPanels());

loadPanels();
renderPanelShells();
refresh();
setInterval(refresh, REFRESH_MS);
