import express from "express";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const dbPath = process.env.DB_PATH ? path.resolve(process.env.DB_PATH) : path.join(__dirname, "db.json");
const app = express();
const port = Number(process.env.PORT || 8787);

// Nothing the panel sends is anywhere near this large.
app.use(express.json({ limit: "64kb" }));

// Bounded like the firmware's EventLog ring buffer: oldest entries drop
// first so a long-running demo can't grow db.json without limit.
const MAX_STORED = 500;
const UID_PATTERN = /^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){3,9}$/;

function readDb() {
  return JSON.parse(fs.readFileSync(dbPath, "utf8"));
}

function writeDb(db) {
  fs.writeFileSync(dbPath, JSON.stringify(db, null, 2) + "\n");
}

function sendError(res, status, code, message) {
  res.status(status).json({ error: { code, message } });
}

function pushBounded(list, item) {
  list.push(item);
  if (list.length > MAX_STORED) {
    list.splice(0, list.length - MAX_STORED);
  }
}

app.get("/health", (_req, res) => {
  res.json({ ok: true, service: "crowpanel-aiot-mock-api" });
});

app.post("/events", (req, res) => {
  if (typeof req.body !== "object" || req.body === null || Array.isArray(req.body)) {
    return sendError(res, 400, "invalid_body", "Request body must be a JSON object.");
  }
  const db = readDb();
  const event = {
    id: `evt-${Date.now()}`,
    source: String(req.body.source || "unknown").slice(0, 64),
    message: String(req.body.message || "No message supplied").slice(0, 256),
    created_at: new Date().toISOString(),
    data: req.body
  };
  pushBounded(db.events, event);
  writeDb(db);
  res.status(201).json(event);
});

app.get("/events", (req, res) => {
  const limit = Math.min(Math.max(Number(req.query.limit) || 100, 1), MAX_STORED);
  const events = readDb().events;
  res.json(events.slice(-limit));
});

app.post("/summary", (req, res) => {
  const prompt = req.body?.prompt || "Summarize the latest CrowPanel demo state.";
  res.json({
    summary: `Mock AI summary for: ${prompt}`,
    action_items: ["Verify board revision", "Keep mock mode on until wiring is confirmed"]
  });
});

app.get("/badges", (_req, res) => {
  res.json(readDb().badges);
});

app.post("/badges", (req, res) => {
  const uid = req.body?.uid || "00:00:00:00";
  if (!UID_PATTERN.test(uid)) {
    return sendError(res, 400, "invalid_uid", "uid must look like AA:BB:CC:DD (4-10 hex byte pairs).");
  }
  const zones = Array.isArray(req.body?.allowed_zones) ? req.body.allowed_zones : ["demo"];
  if (zones.length > 16 || zones.some((z) => typeof z !== "string" || z.length > 32)) {
    return sendError(res, 400, "invalid_zones", "allowed_zones: up to 16 strings of 32 chars each.");
  }
  const db = readDb();
  if (db.badges.some((b) => b.uid.toUpperCase() === uid.toUpperCase())) {
    return sendError(res, 409, "duplicate_uid", `A badge with uid ${uid} already exists.`);
  }
  const badge = {
    badge_id: req.body?.badge_id || `badge-${Date.now()}`,
    uid,
    name: String(req.body?.name || "Unnamed Badge").slice(0, 64),
    role: String(req.body?.role || "guest").slice(0, 32),
    status: String(req.body?.status || "active").slice(0, 32),
    allowed_zones: zones,
    created_at: req.body?.created_at || new Date().toISOString()
  };
  db.badges.push(badge);
  writeDb(db);
  res.status(201).json(badge);
});

app.post("/inspection", (req, res) => {
  const result = req.body?.result || "pass";
  if (result !== "pass" && result !== "fail") {
    return sendError(res, 400, "invalid_result", 'result must be "pass" or "fail".');
  }
  const db = readDb();
  const inspection = {
    id: `insp-${Date.now()}`,
    station: String(req.body?.station || "vision-guard").slice(0, 64),
    qr: String(req.body?.qr || "QR-DEMO-001").slice(0, 128),
    result,
    created_at: new Date().toISOString()
  };
  pushBounded(db.inspections, inspection);
  writeDb(db);
  res.status(201).json(inspection);
});

// Fake controllable device for project 04 (RelayOps). Point a ControlDevice's
// host at this server and path at /gpio, and every hub command lands here:
//   GET /gpio?pin=<n>&state=<0|1>
// It just logs and echoes so you can watch commands arrive with no ESP32 node
// wired up. POST is accepted too (some node firmwares prefer it).
function handleGpio(req, res) {
  const src = req.method === "GET" ? req.query : { ...req.query, ...(req.body || {}) };
  const pin = Number(src.pin);
  const state = Number(src.state) ? 1 : 0;
  if (!Number.isInteger(pin) || pin < 0) {
    return sendError(res, 400, "invalid_pin", "pin must be a non-negative integer.");
  }
  console.log(`[gpio] pin=${pin} state=${state ? "ON" : "OFF"}`);
  res.json({ ok: true, pin, state });
}
app.get("/gpio", handleGpio);
app.post("/gpio", handleGpio);

app.use((_req, res) => {
  sendError(res, 404, "not_found", "Unknown route. See mock-api/README.md for endpoints.");
});

// Also catches malformed JSON bodies from express.json, which would
// otherwise surface as an HTML error page.
app.use((err, _req, res, _next) => {
  const status = err.status || err.statusCode || 500;
  sendError(res, status, status === 400 ? "invalid_json" : "internal_error", err.message);
});

app.listen(port, () => {
  console.log(`CrowPanel mock API listening on http://localhost:${port}`);
});
