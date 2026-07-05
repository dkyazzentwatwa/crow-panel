import express from "express";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const dbPath = process.env.DB_PATH ? path.resolve(process.env.DB_PATH) : path.join(__dirname, "db.json");
const app = express();
const port = Number(process.env.PORT || 8787);

app.use(express.json({ limit: "1mb" }));

function readDb() {
  return JSON.parse(fs.readFileSync(dbPath, "utf8"));
}

function writeDb(db) {
  fs.writeFileSync(dbPath, JSON.stringify(db, null, 2) + "\n");
}

app.get("/health", (_req, res) => {
  res.json({ ok: true, service: "crowpanel-aiot-mock-api" });
});

app.post("/events", (req, res) => {
  const db = readDb();
  const event = {
    id: `evt-${Date.now()}`,
    source: req.body?.source || "unknown",
    message: req.body?.message || "No message supplied",
    created_at: new Date().toISOString(),
    data: req.body || {}
  };
  db.events.push(event);
  writeDb(db);
  res.status(201).json(event);
});

app.get("/events", (_req, res) => {
  res.json(readDb().events);
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
  const db = readDb();
  const badge = {
    badge_id: req.body?.badge_id || `badge-${Date.now()}`,
    uid: req.body?.uid || "00:00:00:00",
    name: req.body?.name || "Unnamed Badge",
    role: req.body?.role || "guest",
    status: req.body?.status || "active",
    allowed_zones: Array.isArray(req.body?.allowed_zones) ? req.body.allowed_zones : ["demo"],
    created_at: req.body?.created_at || new Date().toISOString()
  };
  db.badges.push(badge);
  writeDb(db);
  res.status(201).json(badge);
});

app.post("/inspection", (req, res) => {
  const db = readDb();
  const inspection = {
    id: `insp-${Date.now()}`,
    station: req.body?.station || "vision-guard",
    qr: req.body?.qr || "QR-DEMO-001",
    result: req.body?.result || "pass",
    created_at: new Date().toISOString()
  };
  db.inspections.push(inspection);
  writeDb(db);
  res.status(201).json(inspection);
});

app.listen(port, () => {
  console.log(`CrowPanel mock API listening on http://localhost:${port}`);
});
