const $ = (id) => document.getElementById(id);

const CHART_COLORS = [
  "#67d7a5",
  "#7aa8ff",
  "#f2c45d",
  "#f06d6d",
  "#63d6e8",
  "#d69bf7",
];

const state = {
  params: new Map(),
  editing: null,
};

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
    return value.toFixed(3);
  }
  if (typeof value === "boolean") return value ? "true" : "false";
  return String(value ?? "--");
}

function formatEntryValue(entry) {
  if (!entry) return "--";
  if (entry.type === "int") return String(entry.value ?? "--");
  return formatValue(entry.value);
}

function setText(id, value) {
  const element = $(id);
  if (element) element.textContent = value;
}

function renderSystem(status) {
  const runtimeStatus = status.runtime_status ?? "--";
  setText("statusBadge", runtimeStatus);
  setText("runtimeStatus", runtimeStatus);
  setText("taskState", status.task_name ?? "--");
  setText("loopHz", formatValue(status.loop_hz));
  setText("updateMs", formatValue(status.update_ms));
  setText("frameIndex", String(status.frame_index ?? "--"));
  setText("webuiState", status.webui ? "On" : "Off");
}

function renderParams(params) {
  const body = $("paramsBody");
  const active = document.activeElement;
  const activeKey = active?.dataset?.key;
  state.params = new Map(params.map((p) => [p.key, p]));

  body.innerHTML = "";
  for (const param of params) {
    const tr = document.createElement("tr");

    const key = document.createElement("td");
    key.className = "key";
    key.textContent = param.key;

    const value = document.createElement("td");
    const input = document.createElement("input");
    input.type = "text";
    input.dataset.key = param.key;
    input.value = formatValue(param.value);
    input.disabled = !param.mutable;
    input.addEventListener("focus", () => {
      state.editing = param.key;
    });
    input.addEventListener("blur", () => {
      if (state.editing === param.key) state.editing = null;
    });
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") setParam(param.key, input.value);
    });
    value.appendChild(input);

    const type = document.createElement("td");
    type.textContent = param.type;

    const action = document.createElement("td");
    const button = document.createElement("button");
    button.textContent = "Set";
    button.disabled = !param.mutable;
    button.addEventListener("click", () => setParam(param.key, input.value));
    action.appendChild(button);

    tr.append(key, value, type, action);
    body.appendChild(tr);
  }

  if (activeKey) {
    const next = document.querySelector(`input[data-key="${CSS.escape(activeKey)}"]`);
    if (next && state.editing === activeKey) next.focus();
  }
}

async function setParam(key, value) {
  await api("/api/params/set", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ key, value }),
  }).catch((error) => console.error(error));
}

function renderDebugValues(values = []) {
  const grid = $("debugValuesGrid");
  grid.innerHTML = "";
  const taskValues = [...values].sort((a, b) => a.key.localeCompare(b.key));

  if (!taskValues.length) {
    const empty = document.createElement("div");
    empty.className = "empty-debug";
    empty.textContent = "No debug values";
    grid.appendChild(empty);
    return;
  }

  for (const entry of taskValues) {
    const item = document.createElement("div");
    item.className = `debug-value-item ${entry.type}`;
    const key = document.createElement("span");
    key.textContent = entry.key;
    const value = document.createElement("strong");
    value.textContent = formatEntryValue(entry);
    item.append(key, value);
    grid.appendChild(item);
  }
}

function setupCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(320, Math.round(rect.width || canvas.clientWidth || 720));
  const height = Math.max(220, Math.round(rect.height || canvas.clientHeight || 300));
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

function chooseDebugSeries(history) {
  const keys = Object.keys(history ?? {})
    .filter((key) => Array.isArray(history[key]) && history[key].length >= 2)
    .sort();

  const preferredGroups = [
    ["spr.command.yaw_deg", "spr.command.pitch_deg"],
    ["command.yaw", "command.pitch"],
    ["spr.plan.yaw", "spr.plan.pitch"],
  ];

  const selected = [];
  for (const group of preferredGroups) {
    for (const key of group) {
      if (keys.includes(key) && !selected.includes(key)) selected.push(key);
    }
    if (selected.length) {
      return selected.map((key, index) => ({
        key,
        color: CHART_COLORS[index % CHART_COLORS.length],
        points: history[key],
      }));
    }
  }

  for (const key of keys) {
    if (selected.length >= 6) break;
    if (!selected.includes(key)) selected.push(key);
  }

  return selected.map((key, index) => ({
    key,
    color: CHART_COLORS[index % CHART_COLORS.length],
    points: history[key],
  }));
}

function renderLegend(series) {
  const legend = $("chartLegend");
  legend.innerHTML = "";
  for (const item of series) {
    const latest = item.points[item.points.length - 1]?.[1];
    const pill = document.createElement("div");
    pill.className = "legend-item";
    const dot = document.createElement("span");
    dot.className = "legend-dot";
    dot.style.background = item.color;
    const key = document.createElement("span");
    key.className = "legend-key";
    key.textContent = item.key;
    const value = document.createElement("span");
    value.className = "legend-value";
    value.textContent = formatValue(latest);
    pill.append(dot, key, value);
    legend.appendChild(pill);
  }
}

function drawEmptyChart(ctx, width, height, message) {
  ctx.fillStyle = "#0b0d11";
  ctx.fillRect(0, 0, width, height);
  ctx.fillStyle = "#76808f";
  ctx.font = "13px ui-monospace, monospace";
  ctx.textAlign = "center";
  ctx.fillText(message, width / 2, height / 2);
  ctx.textAlign = "start";
}

function drawDebugChart(history) {
  const canvas = $("chart");
  const { ctx, width, height } = setupCanvas(canvas);
  const series = chooseDebugSeries(history);
  renderLegend(series);

  if (!series.length) {
    drawEmptyChart(ctx, width, height, "No debug scalar history");
    return;
  }

  const margins = { left: 58, right: 18, top: 18, bottom: 36 };
  const plotX = margins.left;
  const plotY = margins.top;
  const plotW = width - margins.left - margins.right;
  const plotH = height - margins.top - margins.bottom;
  const allPoints = series.flatMap((item) => item.points);
  const minT = Math.min(...allPoints.map((p) => p[0]));
  const maxT = Math.max(...allPoints.map((p) => p[0]));
  let minY = Math.min(...allPoints.map((p) => p[1]));
  let maxY = Math.max(...allPoints.map((p) => p[1]));

  if (minY === maxY) {
    minY -= 1;
    maxY += 1;
  } else {
    const padding = (maxY - minY) * 0.12;
    minY -= padding;
    maxY += padding;
  }

  const xFor = (t) => plotX + ((t - minT) / Math.max(maxT - minT, 1)) * plotW;
  const yFor = (v) => plotY + ((maxY - v) / (maxY - minY)) * plotH;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b0d11";
  ctx.fillRect(0, 0, width, height);

  ctx.save();
  ctx.strokeStyle = "#27303b";
  ctx.fillStyle = "#8c96a5";
  ctx.font = "11px ui-monospace, monospace";
  ctx.lineWidth = 1;

  for (let i = 0; i <= 4; i++) {
    const ratio = i / 4;
    const y = plotY + ratio * plotH;
    const value = maxY - ratio * (maxY - minY);
    ctx.beginPath();
    ctx.moveTo(plotX, y);
    ctx.lineTo(plotX + plotW, y);
    ctx.stroke();
    ctx.fillText(formatValue(value), 8, y + 4);
  }

  for (let i = 0; i <= 4; i++) {
    const ratio = i / 4;
    const x = plotX + ratio * plotW;
    const t = minT + ratio * (maxT - minT);
    const secondsAgo = (maxT - t) / 1000;
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
    const points = item.points;
    ctx.save();
    ctx.beginPath();
    points.forEach((p, index) => {
      const x = xFor(p[0]);
      const y = yFor(p[1]);
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

    const last = points[points.length - 1];
    ctx.shadowBlur = 0;
    ctx.fillStyle = item.color;
    ctx.beginPath();
    ctx.arc(xFor(last[0]), yFor(last[1]), 3.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }
}

function renderLogs(logs) {
  const box = $("logs");
  box.innerHTML = "";
  for (const entry of logs.slice(-80).reverse()) {
    const line = document.createElement("div");
    line.className = `log-line ${entry.level.toLowerCase()}`;
    const level = document.createElement("b");
    level.textContent = entry.level;
    const msg = document.createElement("span");
    msg.textContent = entry.message;
    line.append(level, msg);
    box.appendChild(line);
  }
}

async function refresh() {
  try {
    const [status, params, logs, debug] = await Promise.all([
      api("/api/runtime/status"),
      api("/api/params"),
      api("/api/logs"),
      api("/api/debug/list"),
    ]);
    renderSystem(status);
    if (!state.editing) renderParams(params.params);
    renderDebugValues(debug.values);
    drawDebugChart(debug.history);
    renderLogs(logs.logs);
    const debugImageKey = debug.images.includes("spr.fake.overlay")
      ? "spr.fake.overlay"
      : debug.images.includes("spr.overlay")
        ? "spr.overlay"
        : debug.images[0];
    if (debugImageKey) {
      $("debugImage").alt = debugImageKey;
      $("debugImage").src = `/api/debug/image/${encodeURIComponent(debugImageKey)}?t=${Date.now()}`;
    } else {
      $("debugImage").removeAttribute("src");
      $("debugImage").alt = "No debug image";
    }
  } catch (error) {
    setText("statusBadge", "Disconnected");
    setText("runtimeStatus", "Disconnected");
  }
}

for (const button of document.querySelectorAll("[data-command]")) {
  button.addEventListener("click", () => postCommand(button.dataset.command));
}

refresh();
setInterval(refresh, 500);
