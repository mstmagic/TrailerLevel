// main.cpp : ESP32 (ESP32-S3/C3) + MPU-6050/6500 + Wi-Fi AP + HTTP UI + Captive Portal
// - rfetick/MPU6050_light
// - I2C at 400 kHz
// - /sensor JSON: pos_pitch/roll (raw & calibrated), accel/gyro (raw + oriented components)
// - /calibrate (POST): captures current pose as zero
// - /orientation (GET/POST): map sensor axes to trailer axes
// - /wifi (POST): change AP SSID/password (hot-apply)
// - Web UI served at /ui (mobile friendly)
// - Captive portal: wildcard DNS + 302 to /ui for Android/iOS/Windows checks
// - No yaw; “turn” uses gyro around trailer up axis

#include <Arduino.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <DNSServer.h>        // <-- captive portal DNS
#include "config.h"           // SDA_PIN, SCL_PIN, DEFAULT_SSID, DEFAULT_PASSWORD
#include "web_ui.h"           // setupWebUI(WebServer&, const char*)

// -------- Debug macros --------
#ifdef DEBUG_SERIAL
  #define DEBUG_PRINT(x)   Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// -------- Devices --------
static constexpr uint8_t MPU_ADDR = 0x68; // change to 0x69 if AD0 high
MPU6050 mpu(Wire);
WebServer server(80);
Preferences prefs;

// -------- Captive portal DNS --------
DNSServer dnsServer;
IPAddress apIP(192,168,4,1);

// -------- Orientation mapping --------
static int8_t ori_forward = +1;
static int8_t ori_right   = +2;
static int8_t ori_up      = +3;

// -------- Position, accel, gyro, offsets --------
static float pos_pitch_raw = 0.0f, pos_roll_raw = 0.0f;                     // deg
static float pos_pitch_calibrated = 0.0f, pos_roll_calibrated = 0.0f;       // deg
static float pos_pitch_zero = 0.0f, pos_roll_zero  = 0.0f;                   // stored zeros

static float accel_x_raw = 0.0f, accel_y_raw = 0.0f, accel_z_raw = 0.0f;    // sensor frame
static float gyro_x_raw  = 0.0f, gyro_y_raw  = 0.0f, gyro_z_raw  = 0.0f;    // sensor frame

// trailer-oriented accelerations
static float accel_forward = 0.0f, accel_backward = 0.0f;
static float accel_right   = 0.0f, accel_left     = 0.0f;
static float accel_up      = 0.0f, accel_down     = 0.0f;

// trailer-oriented gyro split into signed magnitudes
static float gyro_pitchup = 0.0f,  gyro_pitchdown = 0.0f;   // about trailer right axis
static float gyro_rollright = 0.0f, gyro_rollleft = 0.0f;   // about trailer forward axis
static float gyro_turnright = 0.0f, gyro_turnleft = 0.0f;   // about trailer up axis

// Serial cadence: 15 seconds
static unsigned long lastPrint = 0;
static const uint32_t PRINT_MS = 15000;

// Wi-Fi pending reconfig (after /wifi)
struct {
  bool apply = false;
  String ssid;
  String password;
  uint32_t at_ms = 0;  // when to apply (millis)
} g_wifiPending;

static float finiteOr(float v, float fb = 0.0f) { return isfinite(v) ? v : fb; }

// ---------------- Low-level I2C helpers for robust gyro fallback ----------------
static bool i2cRead(uint8_t dev, uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(dev);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false; // repeated start
  size_t n = Wire.requestFrom(dev, (uint8_t)len);
  if (n != len) return false;
  for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

static bool readGyroRawRegs(int16_t& rx, int16_t& ry, int16_t& rz) {
  uint8_t b[6];
  if (!i2cRead(MPU_ADDR, 0x43, b, 6)) return false; // GYRO_XOUT_H..ZOUT_L
  rx = (int16_t)((b[0] << 8) | b[1]);
  ry = (int16_t)((b[2] << 8) | b[3]);
  rz = (int16_t)((b[4] << 8) | b[5]);
  return true;
}

static float gyroLSBperDPS() {
  uint8_t cfg = 0;
  if (!i2cRead(MPU_ADDR, 0x1B, &cfg, 1)) return 131.0f; // default ±250 dps
  uint8_t fs = (cfg >> 3) & 0x03; // FS_SEL
  switch (fs) {
    case 0: return 131.0f; // ±250 dps
    case 1: return 65.5f;  // ±500 dps
    case 2: return 32.8f;  // ±1000 dps
    case 3: return 16.4f;  // ±2000 dps
  }
  return 131.0f;
}

// ---------------- Orientation helpers ----------------
static float mapAxis(float x, float y, float z, int8_t code) {
  float v = 0.0f;
  switch (abs(code)) {
    case 1: v = x; break;
    case 2: v = y; break;
    case 3: v = z; break;
    default: v = 0.0f; break;
  }
  return (code < 0) ? -v : v;
}

static bool validateOrientation(int8_t f, int8_t r, int8_t u) {
  int af = abs(f), ar = abs(r), au = abs(u);
  if (af < 1 || af > 3 || ar < 1 || ar > 3 || au < 1 || au > 3) return false;
  if (af == ar || af == au || ar == au) return false;
  return true;
}

static int8_t parseAxisCode(const String& s, bool& ok) {
  String t = s; t.trim(); t.toUpperCase(); ok = true;
  if (t == "X")  return +1; if (t == "-X") return -1;
  if (t == "Y")  return +2; if (t == "-Y") return -2;
  if (t == "Z")  return +3; if (t == "-Z") return -3;
  if (t == "1")  return +1; if (t == "-1") return -1;
  if (t == "2")  return +2; if (t == "-2") return -2;
  if (t == "3")  return +3; if (t == "-3") return -3;
  ok = false; return +1;
}

static void saveOrientation() {
  prefs.begin("imu", false);
  prefs.putChar("oriF", ori_forward);
  prefs.putChar("oriR", ori_right);
  prefs.putChar("oriU", ori_up);
  prefs.end();
  DEBUG_PRINT("Saved orientation F/R/U = ");
  DEBUG_PRINT(ori_forward); DEBUG_PRINT("/");
  DEBUG_PRINT(ori_right); DEBUG_PRINT("/");
  DEBUG_PRINTLN(ori_up);
}

static void loadOrientation() {
  prefs.begin("imu", true);
  bool hasF = prefs.isKey("oriF");
  bool hasR = prefs.isKey("oriR");
  bool hasU = prefs.isKey("oriU");
  if (hasF && hasR && hasU) {
    ori_forward = prefs.getChar("oriF", +1);
    ori_right   = prefs.getChar("oriR", +2);
    ori_up      = prefs.getChar("oriU", +3);
  }
  prefs.end();
  if (!validateOrientation(ori_forward, ori_right, ori_up)) {
    ori_forward = +1; ori_right = +2; ori_up = +3;
  }
  DEBUG_PRINT("Loaded orientation F/R/U = ");
  DEBUG_PRINT(ori_forward); DEBUG_PRINT("/");
  DEBUG_PRINT(ori_right); DEBUG_PRINT("/");
  DEBUG_PRINTLN(ori_up);
}

// ---------------- IMU bring-up ----------------
static bool initIMU() {
  Wire.setClock(400000);
  Wire.setTimeOut(1000);

  byte status = mpu.begin(MPU_ADDR);
  if (status != 0) {
    DEBUG_PRINTLN("MPU begin failed");
    return false;
  }

  for (int i = 0; i < 30; i++) { mpu.update(); delay(5); }
  DEBUG_PRINTLN("MPU ready");
  return true;
}

// ---------------- Preferences helpers ----------------
static void saveCalibration() {
  prefs.begin("imu", false);
  prefs.putFloat("pitch_zero", pos_pitch_zero);
  prefs.putFloat("roll_zero",  pos_roll_zero);
  prefs.end();
  DEBUG_PRINTLN("Saved calibration to Preferences");
}

static void loadOrBootstrapCalibration() {
  prefs.begin("imu", true);
  bool hasPitch = prefs.isKey("pitch_zero") || prefs.isKey("pitch_off");
  bool hasRoll  = prefs.isKey("roll_zero")  || prefs.isKey("roll_off");
  if (hasPitch && hasRoll) {
    pos_pitch_zero = prefs.getFloat(prefs.isKey("pitch_zero") ? "pitch_zero" : "pitch_off", 0.0f);
    pos_roll_zero  = prefs.getFloat(prefs.isKey("roll_zero")  ? "roll_zero"  : "roll_off",  0.0f);
    prefs.end();
    DEBUG_PRINT("Loaded calibration zero p/r = ");
    DEBUG_PRINT(pos_pitch_zero); DEBUG_PRINT("/");
    DEBUG_PRINTLN(pos_roll_zero);
    return;
  }
  prefs.end();

  // No saved calibration -> bootstrap from accelerometer-based pose
  for (int i = 0; i < 30; i++) { mpu.update(); delay(5); }

  accel_x_raw = mpu.getAccX(); accel_y_raw = mpu.getAccY(); accel_z_raw = mpu.getAccZ();

  float fwd = mapAxis(accel_x_raw, accel_y_raw, accel_z_raw, ori_forward);
  float rgt = mapAxis(accel_x_raw, accel_y_raw, accel_z_raw, ori_right);
  float upv = mapAxis(accel_x_raw, accel_y_raw, accel_z_raw, ori_up);

  float denom = sqrtf(rgt * rgt + upv * upv); if (denom < 1e-6f) denom = 1e-6f;
  pos_pitch_raw = atan2f(-fwd, denom) * 180.0f / PI;
  pos_roll_raw  = atan2f( rgt, upv ) * 180.0f / PI;

  pos_pitch_zero = finiteOr(pos_pitch_raw);
  pos_roll_zero  = finiteOr(pos_roll_raw);
  saveCalibration();

  DEBUG_PRINT("Bootstrapped zeros p/r = ");
  DEBUG_PRINT(pos_pitch_zero); DEBUG_PRINT("/");
  DEBUG_PRINTLN(pos_roll_zero);
}

// ---------------- Sensor read and derive values ----------------
static void readIMU() {
  mpu.update();

  accel_x_raw = mpu.getAccX();
  accel_y_raw = mpu.getAccY();
  accel_z_raw = mpu.getAccZ();

  gyro_x_raw = mpu.getGyroX();
  gyro_y_raw = mpu.getGyroY();
  gyro_z_raw = mpu.getGyroZ();

  // Robust gyro fallback if non-finite
  if (!isfinite(gyro_x_raw) || !isfinite(gyro_y_raw) || !isfinite(gyro_z_raw)) {
    int16_t grx, gry, grz;
    if (readGyroRawRegs(grx, gry, grz)) {
      float lsb = gyroLSBperDPS();
      gyro_x_raw = (float)grx / lsb;
      gyro_y_raw = (float)gry / lsb;
      gyro_z_raw = (float)grz / lsb;
      DEBUG_PRINTLN("Gyro fallback used (raw to dps)");
    } else {
      gyro_x_raw = gyro_y_raw = gyro_z_raw = 0.0f;
      DEBUG_PRINTLN("Gyro fallback failed, forcing 0 dps");
    }
  }

  // Map accel into trailer frame
  accel_forward = mapAxis(accel_x_raw, accel_y_raw, accel_z_raw, ori_forward);
  accel_right   = mapAxis(accel_x_raw, accel_y_raw, accel_z_raw, ori_right);
  accel_up      = mapAxis(accel_x_raw, accel_y_raw, accel_z_raw, ori_up);
  accel_backward = -accel_forward;
  accel_left     = -accel_right;
  accel_down     = -accel_up;

  // Compute pitch and roll from oriented accel only
  float denom = sqrtf(accel_right * accel_right + accel_up * accel_up); if (denom < 1e-6f) denom = 1e-6f;
  pos_pitch_raw = atan2f(-accel_forward, denom) * 180.0f / PI;
  pos_roll_raw  = atan2f( accel_right,   accel_up ) * 180.0f / PI;

  auto wrap180 = [](float a)->float {
    if (!isfinite(a)) return 0.0f;
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
  };

  pos_pitch_calibrated = wrap180(pos_pitch_raw - pos_pitch_zero);
  pos_roll_calibrated  = wrap180(pos_roll_raw  - pos_roll_zero);

  // Map gyro into trailer frame and split into directional components
  float gyro_forward = mapAxis(gyro_x_raw, gyro_y_raw, gyro_z_raw, ori_forward);
  float gyro_right   = mapAxis(gyro_x_raw, gyro_y_raw, gyro_z_raw, ori_right);
  float gyro_up      = mapAxis(gyro_x_raw, gyro_y_raw, gyro_z_raw, ori_up);

  gyro_pitchup    = max(0.0f, +gyro_right);
  gyro_pitchdown  = max(0.0f, -gyro_right);
  gyro_rollright  = max(0.0f, +gyro_forward);
  gyro_rollleft   = max(0.0f, -gyro_forward);
  gyro_turnright  = max(0.0f, +gyro_up);
  gyro_turnleft   = max(0.0f, -gyro_up);
}

// ---------------- HTTP helpers (logging and CORS) ----------------
static void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static void sendJson(int code, const String& body) {
  addCORS();
  server.send(code, "application/json", body);
  DEBUG_PRINT("HTTP -> "); DEBUG_PRINTLN(code);
}

static void handleOptions() {
  addCORS();
  server.send(204);
}

// ---------------- Captive portal helpers ----------------
static void startCaptiveDNS() {
  apIP = WiFi.softAPIP();
  dnsServer.stop();
  dnsServer.setTTL(1);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
  DEBUG_PRINT("DNS captive on "); DEBUG_PRINTLN(apIP);
}

static void captiveRedirect() {
  String ui = String("http://") + apIP.toString() + "/ui";
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Location", ui, true);
  // 302 + meta refresh for stubborn CNA clients
  server.send(302, "text/html",
    String(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<meta http-equiv='refresh' content='0; url=")) + ui +
    F("'></head><body style='background:#000;color:#fff;font-family:system-ui'>"
      "<p>Redirecting to <a href='") + ui + F("'>UI</a>…</p></body></html>"));
}

// Register common OS connectivity-check endpoints to trigger captive UI
static void registerCaptiveRoutes() {
  // Root goes to UI
  server.on("/", HTTP_GET, captiveRedirect);

  // Android
  server.on("/generate_204", HTTP_GET, captiveRedirect);
  server.on("/gen_204", HTTP_GET, captiveRedirect);
  server.on("/google/generate_204", HTTP_GET, captiveRedirect);
  server.on("/connectivity-check", HTTP_GET, captiveRedirect);

  // iOS / Apple
  server.on("/hotspot-detect.html", HTTP_GET, captiveRedirect);
  server.on("/success.html", HTTP_GET, captiveRedirect);
  server.on("/library/test/success.html", HTTP_GET, captiveRedirect);

  // Windows (NCSI)
  server.on("/ncsi.txt", HTTP_GET, captiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, captiveRedirect);

  // Chrome/others
  server.on("/canonical.html", HTTP_GET, captiveRedirect);

  // Anything else not found -> UI
  server.onNotFound(captiveRedirect);
}

// ---------------- HTTP handlers ----------------
static void handleSensor() {
  for (int i = 0; i < 5; i++) { mpu.update(); delay(2); }
  readIMU();

  JsonDocument doc;

  // positions
  doc["pos_pitch_raw"]        = pos_pitch_raw;
  doc["pos_roll_raw"]         = pos_roll_raw;
  doc["pos_pitch_calibrated"] = pos_pitch_calibrated;
  doc["pos_roll_calibrated"]  = pos_roll_calibrated;

  // raw accel and gyro in sensor frame
  doc["accel_x_raw"] = accel_x_raw;
  doc["accel_y_raw"] = accel_y_raw;
  doc["accel_z_raw"] = accel_z_raw;

  doc["gyro_x_raw"] = gyro_x_raw;
  doc["gyro_y_raw"] = gyro_y_raw;
  doc["gyro_z_raw"] = gyro_z_raw;

  // trailer oriented acceleration
  doc["accel_forward"]  = accel_forward;
  doc["accel_backward"] = accel_backward;
  doc["accel_right"]    = accel_right;
  doc["accel_left"]     = accel_left;
  doc["accel_up"]       = accel_up;
  doc["accel_down"]     = accel_down;

  // trailer oriented gyro as split components
  doc["gyro_pitchup"]    = gyro_pitchup;
  doc["gyro_pitchdown"]  = gyro_pitchdown;
  doc["gyro_rollright"]  = gyro_rollright;
  doc["gyro_rollleft"]   = gyro_rollleft;
  doc["gyro_turnright"]  = gyro_turnright;
  doc["gyro_turnleft"]   = gyro_turnleft;

  String json;
  serializeJson(doc, json);
  sendJson(200, json);
}

static void handleCalibrate() {
  for (int i = 0; i < 30; i++) { mpu.update(); delay(5); }
  readIMU();

  pos_pitch_zero = finiteOr(pos_pitch_raw);
  pos_roll_zero  = finiteOr(pos_roll_raw);
  saveCalibration();

  JsonDocument resp;
  resp["status"] = "ok";
  resp["zeros"]["pos_pitch_zero"] = pos_pitch_zero;
  resp["zeros"]["pos_roll_zero"]  = pos_roll_zero;

  String json;
  serializeJson(resp, json);
  sendJson(200, json);
}

static void handleGetCalibration() {
  JsonDocument doc;
  doc["pos_pitch_zero"] = pos_pitch_zero;
  doc["pos_roll_zero"]  = pos_roll_zero;
  String json;
  serializeJson(doc, json);
  sendJson(200, json);
}

static void handleResetCalibration() {
  pos_pitch_zero = 0.0f;
  pos_roll_zero  = 0.0f;
  saveCalibration();
  sendJson(200, "{\"status\":\"ok\"}");
}

// GET returns current orientation; POST sets orientation
static void handleOrientation() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    doc["forward"] = ori_forward;
    doc["right"]   = ori_right;
    doc["up"]      = ori_up;

    auto axisName = [](int8_t code)->String{
      String s = (code < 0) ? "-" : "";
      switch (abs(code)) {
        case 1: s += "X"; break;
        case 2: s += "Y"; break;
        case 3: s += "Z"; break;
        default: s += "?"; break;
      }
      return s;
    };
    doc["legend"]["forward"] = axisName(ori_forward);
    doc["legend"]["right"]   = axisName(ori_right);
    doc["legend"]["up"]      = axisName(ori_up);

    String json;
    serializeJson(doc, json);
    sendJson(200, json);
    return;
  }

  if (server.method() == HTTP_POST) {
    int8_t f = ori_forward, r = ori_right, u = ori_up;
    bool okF = true, okR = true, okU = true;

    if (server.hasArg("plain") && server.arg("plain").length() > 0) {
      JsonDocument body;
      DeserializationError err = deserializeJson(body, server.arg("plain"));
      if (!err) {
        if (body["forward"].is<const char*>()) { const char* s = body["forward"].as<const char*>(); f = parseAxisCode(String(s), okF); }
        else if (body["forward"].is<int>())    { int v = body["forward"].as<int>(); okF = (v >= -3 && v <= 3 && v != 0); f = (int8_t)v; }

        if (body["right"].is<const char*>())   { const char* s = body["right"].as<const char*>();   r = parseAxisCode(String(s), okR); }
        else if (body["right"].is<int>())      { int v = body["right"].as<int>(); okR = (v >= -3 && v <= 3 && v != 0); r = (int8_t)v; }

        if (body["up"].is<const char*>())      { const char* s = body["up"].as<const char*>();      u = parseAxisCode(String(s), okU); }
        else if (body["up"].is<int>())         { int v = body["up"].as<int>(); okU = (v >= -3 && v <= 3 && v != 0); u = (int8_t)v; }
      }
    }

    if (server.hasArg("forward")) { bool okT = true; f = parseAxisCode(server.arg("forward"), okT); okF = okF && okT; }
    if (server.hasArg("right"))   { bool okT = true; r = parseAxisCode(server.arg("right"), okT);   okR = okR && okT; }
    if (server.hasArg("up"))      { bool okT = true; u = parseAxisCode(server.arg("up"), okT);      okU = okU && okT; }

    if (!okF || !okR || !okU || !validateOrientation(f, r, u)) {
      sendJson(400, "{\"error\":\"invalid orientation. Use distinct axes from {X,Y,Z} with optional minus.\"}");
      return;
    }

    ori_forward = f; ori_right = r; ori_up = u;
    saveOrientation();

    JsonDocument resp;
    resp["status"] = "ok";
    resp["forward"] = ori_forward;
    resp["right"]   = ori_right;
    resp["up"]      = ori_up;
    String json;
    serializeJson(resp, json);
    sendJson(200, json);
    return;
  }

  sendJson(405, "{\"error\":\"method not allowed\"}");
}

// /wifi (POST) — change AP SSID/password; hot-apply shortly after responding
static void handleWifiUpdate() {
  addCORS();
  if (!server.hasArg("plain") || server.arg("plain").isEmpty()) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) { server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

  const char* ssid_c = body["ssid"] | "";
  const char* pwd_c  = body["password"] | "";
  String newSsid = String(ssid_c);
  String newPwd  = String(pwd_c);

  if (newSsid.length() < 1 || newSsid.length() > 32) {
    server.send(400, "application/json", "{\"error\":\"ssid length must be 1..32\"}");
    return;
  }
  if (newPwd.length() > 0 && newPwd.length() < 8) {
    server.send(400, "application/json", "{\"error\":\"password must be >=8 or empty for open\"}");
    return;
  }

  prefs.begin("wifi", false);
  prefs.putString("ssid", newSsid);
  prefs.putString("password", newPwd);
  prefs.end();

  g_wifiPending.apply = true;
  g_wifiPending.ssid = newSsid;
  g_wifiPending.password = newPwd;
  g_wifiPending.at_ms = millis() + 500; // apply soon after response

  String resp = String("{\"status\":\"ok\",\"ssid\":\"") + newSsid + "\"}";
  server.send(200, "application/json", resp);
}

// ---------------- Wi-Fi AP + events ----------------
static void setupWifiEvents() {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    DEBUG_PRINT("AP client connected AID=");
    DEBUG_PRINT(info.wifi_ap_staconnected.aid);
    DEBUG_PRINTLN("");
  }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);

  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    DEBUG_PRINT("AP client disconnected AID=");
    DEBUG_PRINTLN(info.wifi_ap_stadisconnected.aid);
  }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
}

static bool startAP(const String& ssid, const String& pwd) {
  bool ok = false;
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  if (pwd.length() == 0) ok = WiFi.softAP(ssid.c_str());
  else                   ok = WiFi.softAP(ssid.c_str(), pwd.c_str());
  apIP = WiFi.softAPIP();
  DEBUG_PRINT("softAP("); DEBUG_PRINT(ssid); DEBUG_PRINT(", ****) -> ");
  DEBUG_PRINTLN(ok ? "OK" : "FAIL");
  DEBUG_PRINT("AP IP address: "); DEBUG_PRINTLN(apIP);
  if (ok) startCaptiveDNS();
  return ok;
}

static void setupWifi() {
  setupWifiEvents();

  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", DEFAULT_SSID);
  String password = prefs.getString("password", DEFAULT_PASSWORD);
  prefs.end();

  startAP(ssid, password);
}

// ---------------- Arduino ----------------
void setup() {
#ifdef DEBUG_SERIAL
  Serial.begin(115200);
  delay(200);
  DEBUG_PRINTLN("Serial debug enabled");
#endif

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(10);

  if (!initIMU()) {
    DEBUG_PRINTLN("MPU init failed, continuing so HTTP still works");
  }

  loadOrientation();
  loadOrBootstrapCalibration();
  setupWifi();

  // API Routes
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/calibration", HTTP_GET, handleGetCalibration);
  server.on("/calibration/reset", HTTP_POST, handleResetCalibration);
  server.on("/orientation", HTTP_GET, handleOrientation);
  server.on("/orientation", HTTP_POST, handleOrientation);
  server.on("/wifi", HTTP_POST, handleWifiUpdate);
  server.on("/ui", HTTP_GET, [=](){
    server.send_P(200, "text/html", INDEX_HTML);
  });

  // CORS preflight
  server.on("/sensor", HTTP_OPTIONS, handleOptions);
  server.on("/calibrate", HTTP_OPTIONS, handleOptions);
  server.on("/calibration", HTTP_OPTIONS, handleOptions);
  server.on("/calibration/reset", HTTP_OPTIONS, handleOptions);
  server.on("/orientation", HTTP_OPTIONS, handleOptions);
  server.on("/wifi", HTTP_OPTIONS, handleOptions);


  // Captive portal redirects & wildcard DNS
  registerCaptiveRoutes();

  server.begin();
  DEBUG_PRINTLN("HTTP server started");
}

void loop() {
  dnsServer.processNextRequest();       // captive DNS
  server.handleClient();
  mpu.update();

  // Apply pending Wi-Fi change after response has been sent
  if (g_wifiPending.apply && (int32_t)(millis() - g_wifiPending.at_ms) >= 0) {
    g_wifiPending.apply = false;
    DEBUG_PRINTLN("Reconfiguring AP with new credentials...");
    WiFi.softAPdisconnect(true);
    delay(150);
    if (!startAP(g_wifiPending.ssid, g_wifiPending.password)) {
      DEBUG_PRINTLN("New AP failed; falling back to defaults");
      WiFi.softAPdisconnect(true);
      delay(150);
      startAP(DEFAULT_SSID, DEFAULT_PASSWORD);
    }
  }

  if (millis() - lastPrint >= PRINT_MS) {
    lastPrint = millis();
    readIMU();

    DEBUG_PRINT("IMU: pos_raw[p="); DEBUG_PRINT(pos_pitch_raw);
    DEBUG_PRINT(", r="); DEBUG_PRINT(pos_roll_raw);
    DEBUG_PRINT("]  pos_cal[p="); DEBUG_PRINT(pos_pitch_calibrated);
    DEBUG_PRINT(", r="); DEBUG_PRINT(pos_roll_calibrated);
    DEBUG_PRINT("]  accel_sensor["); DEBUG_PRINT(accel_x_raw); DEBUG_PRINT(", "); DEBUG_PRINT(accel_y_raw); DEBUG_PRINT(", "); DEBUG_PRINT(accel_z_raw);
    DEBUG_PRINT("]  gyro_sensor["); DEBUG_PRINT(gyro_x_raw); DEBUG_PRINT(", "); DEBUG_PRINT(gyro_y_raw); DEBUG_PRINT(", "); DEBUG_PRINT(gyro_z_raw);
    DEBUG_PRINT("]  accel_trailer[fwd="); DEBUG_PRINT(accel_forward); DEBUG_PRINT(", rgt="); DEBUG_PRINT(accel_right); DEBUG_PRINT(", up="); DEBUG_PRINT(accel_up);
    DEBUG_PRINT("]  gyro_trailer[pup="); DEBUG_PRINT(gyro_pitchup); DEBUG_PRINT(", rrt="); DEBUG_PRINT(gyro_rollright); DEBUG_PRINT(", trt="); DEBUG_PRINT(gyro_turnright); DEBUG_PRINTLN("]");
  }
}
