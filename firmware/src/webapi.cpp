// SPDX-License-Identifier: MIT
// REST API + раздача web UI из LittleFS.
// Синхронный WebServer выбран намеренно: асинхронные библиотеки на ESP32-C6
// с Arduino core 3.x пока капризны, а нагрузка здесь копеечная.

#include "config.h"
#include "faults.h"
#include "hal/hardware.h"
#include "scenario.h"
#include "webapi.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>

static WebServer server(HTTP_PORT);

static void sendJson(JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json; charset=utf-8", out);
}

static void sendError(const char *msg, int code = 400) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(doc, code);
}

static bool bodyJson(JsonDocument &doc) {
  if (!server.hasArg("plain")) return false;
  return deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok;
}

// ------------------------------------------------------------------- /api/state
static void handleState() {
  JsonDocument doc;
  hal::Board &b = hal::board;

  doc["fw"] = FW_VERSION;
  doc["uptime_s"] = millis() / 1000;

  JsonObject run = doc["run"].to<JsonObject>();
  const char *st = scenario.state() == RunState::Running    ? "running"
                   : scenario.state() == RunState::Finished ? "finished"
                                                            : "idle";
  run["state"] = st;
  run["id"] = scenario.id();
  run["title"] = scenario.title();
  run["step"] = scenario.stepIndex();
  run["steps"] = scenario.stepCount();
  run["elapsed_ms"] = scenario.elapsedMs();
  run["quantity"] = scenario.currentQuantity();
  run["hint"] = scenario.currentHint();
  run["truth"] = isnan(scenario.currentTruth()) ? JsonVariant() : JsonVariant(scenario.currentTruth());
  run["truth_samples"] = scenario.truthCount();

  JsonObject hw = doc["hw"].to<JsonObject>();
  hw["reference_tap"] = b.reference.tap();
  hw["reference_mv"] = b.reference.millivolts();
  hw["level_mv"] = b.level.millivolts();
  hw["decade_channel"] = b.decade.channel();
  hw["decade_label"] = b.decade.label();
  hw["component_channel"] = b.zoo.channel();
  hw["ghost"] = b.ghost.enabled();
  hw["chatter"] = b.chatter.enabled();
  hw["heater"] = b.thermal.heater();
  hw["fan"] = b.thermal.fan();
  hw["loop_percent"] = b.loop.percent();
  hw["loop_ma"] = b.loop.milliamps();
  hw["load_ma"] = b.loads.expectedCurrentMa();

  JsonObject sig = hw["signal"].to<JsonObject>();
  sig["on"] = b.signal.enabled();
  sig["hz"] = b.signal.freqHz();
  sig["duty"] = b.signal.duty();
  sig["dc_mv"] = b.signal.expectedDcMv();
  sig["ac_rms_mv"] = b.signal.expectedAcRmsMv();
  sig["acdc_rms_mv"] = b.signal.expectedAcDcRmsMv();
  sig["crest"] = b.signal.crestFactor();

  const float refC = b.thermal.readReferenceC();
  if (!isnan(refC)) hw["reference_c"] = refC;

  JsonObject fault = doc["fault"].to<JsonObject>();
  fault["active"] = faults.active();
  fault["solved"] = faults.solved();
  fault["attempts"] = faults.attempts();
  if (faults.active()) fault["symptom"] = faults.symptom();
  if (faults.solved() && faults.current()) {
    fault["id"] = faults.current()->id;
    fault["title"] = faults.current()->title;
    fault["method"] = faults.current()->method;
  }

  sendJson(doc);
}

// --------------------------------------------------------------- сценарии
static void handleScenarios() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  ScenarioEngine::listScenarios(arr);
  sendJson(doc);
}

static void handleScenarioStart() {
  JsonDocument body;
  if (!bodyJson(body)) return sendError("ожидается JSON {\"id\":\"...\"}");
  const char *id = body["id"] | "";
  if (!*id) return sendError("не задан id сценария");
  if (!scenario.start(id)) return sendError("сценарий не найден или повреждён", 404);
  handleState();
}

static void handleScenarioStop() {
  scenario.stop();
  handleState();
}

// ------------------------------------------------- ground truth в виде CSV
static void handleTruthCsv() {
  String csv;
  csv.reserve(scenario.truthCount() * 40 + 64);
  csv += "t_ms,step,quantity,value\n";
  for (size_t i = 0; i < scenario.truthCount(); i++) {
    const TruthSample &s = scenario.truthAt(i);
    csv += String(s.tMs);
    csv += ',';
    csv += String(s.stepIndex);
    csv += ',';
    csv += s.quantity;
    csv += ',';
    csv += isnan(s.value) ? String("nan") : String(s.value, 6);
    csv += '\n';
  }
  server.sendHeader("Content-Disposition", "attachment; filename=truth.csv");
  server.send(200, "text/csv; charset=utf-8", csv);
}

// ------------------------------------------------------------- ручной режим
static void handleManual() {
  JsonDocument body;
  if (!bodyJson(body)) return sendError("ожидается JSON");
  hal::Board &b = hal::board;

  if (scenario.state() == RunState::Running) scenario.stop();

  if (body["reference_tap"].is<int>()) b.reference.select(body["reference_tap"]);
  if (body["level_mv"].is<float>()) b.level.setMillivolts(body["level_mv"]);
  if (body["decade"].is<int>()) b.decade.select(body["decade"]);
  if (body["component"].is<int>()) b.zoo.select(body["component"]);
  if (body["ghost"].is<bool>()) b.ghost.enable(body["ghost"]);
  if (body["chatter"].is<bool>()) b.chatter.enable(body["chatter"]);
  if (body["heater"].is<int>()) b.thermal.setHeater(body["heater"]);
  if (body["fan"].is<bool>()) b.thermal.setFan(body["fan"]);
  if (body["loop_percent"].is<float>()) b.loop.setPercent(body["loop_percent"]);
  if (body["inrush_ms"].is<int>()) b.inrush.pulse(body["inrush_ms"]);
  if (body["loads"].is<JsonObject>()) {
    JsonObject l = body["loads"];
    b.loads.set(l["led"] | false, l["mid"] | false, l["high"] | false);
  }
  if (body["signal"].is<JsonObject>()) {
    JsonObject sg = body["signal"];
    b.signal.set(sg["hz"] | b.signal.freqHz(), sg["duty"] | b.signal.duty());
    b.signal.enable(sg["on"] | true);
  }
  handleState();
}

static void handleSafe() {
  scenario.stop();
  faults.stop();
  hal::board.safeState();
  handleState();
}

// ----------------------------------------------------- «найди неисправность»
static void handleFaultCases() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  faults.describeCases(arr);
  sendJson(doc);
}

static void handleFaultStart() {
  JsonDocument body;
  bodyJson(body);
  const char *id = body["id"] | "";
  if (!faults.start(*id ? id : nullptr)) return sendError("неизвестный случай", 404);
  handleState();
}

static void handleFaultAnswer() {
  JsonDocument body;
  if (!bodyJson(body)) return sendError("ожидается JSON {\"id\":\"...\"}");
  const bool correct = faults.answer(body["id"] | "");
  JsonDocument doc;
  doc["correct"] = correct;
  doc["attempts"] = faults.attempts();
  if (correct && faults.current()) {
    doc["title"] = faults.current()->title;
    doc["method"] = faults.current()->method;
  }
  sendJson(doc);
}

static void handleFaultStop() {
  faults.stop();
  handleState();
}

// ------------------------------------------------------------- справочники
static void handleCatalog() {
  JsonDocument doc;
  JsonArray dec = doc["decade"].to<JsonArray>();
  for (uint8_t i = 0; i < hal::ResistorDecade::tableSize(); i++) {
    JsonObject o = dec.add<JsonObject>();
    o["ch"] = i;
    o["label"] = hal::ResistorDecade::table()[i].label;
    const float r = hal::ResistorDecade::table()[i].ohms;
    if (!isnan(r)) o["ohms"] = r;
  }
  JsonArray zoo = doc["components"].to<JsonArray>();
  for (uint8_t i = 0; i < hal::ComponentZoo::tableSize(); i++) {
    const hal::ComponentEntry &e = hal::ComponentZoo::table()[i];
    JsonObject o = zoo.add<JsonObject>();
    o["ch"] = i;
    o["label"] = e.label;
    o["kind"] = e.kind;
    o["faulty"] = e.faulty;
  }
  sendJson(doc);
}

static String contentType(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  return "text/plain; charset=utf-8";
}

static bool serveStatic(String path) {
  if (path.endsWith("/")) path += "index.html";
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  server.streamFile(f, contentType(path));
  f.close();
  return true;
}

void webapi::begin() {
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/catalog", HTTP_GET, handleCatalog);
  server.on("/api/scenarios", HTTP_GET, handleScenarios);
  server.on("/api/scenario/start", HTTP_POST, handleScenarioStart);
  server.on("/api/scenario/stop", HTTP_POST, handleScenarioStop);
  server.on("/api/truth.csv", HTTP_GET, handleTruthCsv);
  server.on("/api/manual", HTTP_POST, handleManual);
  server.on("/api/safe", HTTP_POST, handleSafe);
  server.on("/api/faults", HTTP_GET, handleFaultCases);
  server.on("/api/fault/start", HTTP_POST, handleFaultStart);
  server.on("/api/fault/answer", HTTP_POST, handleFaultAnswer);
  server.on("/api/fault/stop", HTTP_POST, handleFaultStop);

  server.onNotFound([]() {
    if (!serveStatic(server.uri())) server.send(404, "text/plain", "not found");
  });

  server.begin();
  Serial.printf("[web] HTTP на порту %d\n", HTTP_PORT);
}

void webapi::tick() { server.handleClient(); }
