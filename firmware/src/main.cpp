// main.cpp : ESP32 (ESP32-S3/C3) + MPU-6050/6500 + Wi-Fi AP + HTTP UI + Captive Portal
// Key points:
// - Orientation basis (off-axis OK) captured at Calibrate: UP from accel average, forward from +X/-X/+Y/-Y hint.
// - Gravity removal: instantaneous tilt (Level) * measured gravity magnitude (no trailing).
// - Leveling gauge uses EMA (display only) TL_LEVEL_AVG_TAU_MS.
// - Firmware computes peak-hold (with decay) for Acceleration and Roll and exposes them via /sensor.
// - Captive portal & redirects use http://<TL_DOMAIN><TL_WEB_UI_PATH>.
#include <Arduino.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "web_ui.h"

// -------- Debug macros --------
#ifdef DEBUG_SERIAL
  #define DEBUG_PRINT(x)   Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// -------- Devices --------
static constexpr uint8_t MPU_ADDR = 0x68; // change to 0x69 if AD0=HIGH
MPU6050 mpu(Wire);
WebServer server(TL_HTTP_PORT);
Preferences prefs;

// -------- Captive portal DNS --------
DNSServer dnsServer;
IPAddress apIP;

// -------- Wi-Fi pending reconfig (global) --------
struct WifiPending { bool apply; String ssid; String password; uint32_t at_ms; };
static WifiPending g_wifiPending = { false, String(), String(), 0 };

// -------- Math helpers / basis --------
static inline float dot3(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static inline void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1]*b[2]-a[2]*b[1]; out[1] = a[2]*b[0]-a[0]*b[2]; out[2] = a[0]*b[1]-a[1]*b[0];
}
static inline float norm3(const float v[3]) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static inline void normalize3(float v[3]) { float n = norm3(v); if (n < 1e-9f) return; v[0]/=n; v[1]/=n; v[2]/=n; }
static inline void projOntoPlane(const float v[3], const float n[3], float out[3]) {
  float k = dot3(n,v);
  out[0] = v[0]-n[0]*k; out[1] = v[1]-n[1]*k; out[2] = v[2]-n[2]*k;
  normalize3(out);
}
static const float AX_X[3] = {1,0,0}, AX_Y[3] = {0,1,0};

struct OrientBasis {
  float fwd[3];   // trailer forward (unit in sensor frame)
  float rgt[3];   // trailer right
  float up[3];    // trailer up
  bool  valid = false;
} g_basis;

static String g_forwardHint = "+X"; // allowed: +X, -X, +Y, -Y

static void buildBasisFromUpAndHint(const float up_s[3]){
  float up[3] = { up_s[0], up_s[1], up_s[2] };
  normalize3(up);
  const float* base = (g_forwardHint.indexOf('Y')>=0 ? AX_Y : AX_X);
  float sign = (g_forwardHint.startsWith("-") ? -1.0f : 1.0f);
  float cand[3] = { base[0]*sign, base[1]*sign, base[2]*sign };

  float fwd[3]; projOntoPlane(cand, up, fwd);
  float rgt[3]; cross3(fwd, up, rgt); normalize3(rgt);
  float tmp[3]; cross3(up, rgt, tmp); normalize3(tmp);
  fwd[0]=tmp[0]; fwd[1]=tmp[1]; fwd[2]=tmp[2];

  memcpy(g_basis.fwd, fwd, sizeof(fwd));
  memcpy(g_basis.rgt, rgt, sizeof(rgt));
  memcpy(g_basis.up,  up,  sizeof(up));
  g_basis.valid = true;
}

static void toTrailer(float sx, float sy, float sz, float& fwd, float& rgt, float& up){
  if (!g_basis.valid) { fwd = rgt = up = 0; return; }
  float v[3] = { sx, sy, sz };
  fwd = dot3(v, g_basis.fwd);
  rgt = dot3(v, g_basis.rgt);
  up  = dot3(v, g_basis.up);
}

// -------- Pose/accel/gyro, zeros & EMA --------
static float pos_pitch_raw=0, pos_roll_raw=0;                     // deg
static float pos_pitch_calibrated=0, pos_roll_calibrated=0;       // deg
static float pos_pitch_zero=0, pos_roll_zero=0;                   // deg

static float accel_x_raw=0, accel_y_raw=0, accel_z_raw=0;
static float gyro_x_raw=0,  gyro_y_raw=0,  gyro_z_raw=0;

// Accel (gravity removed) in trailer frame
static float accel_forward=0, accel_backward=0;
static float accel_right=0,  accel_left=0;
static float accel_up=0,     accel_down=0;

// Gyro in trailer frame split into signed magnitudes
static float gyro_pitchup=0,  gyro_pitchdown=0;   // about trailer RIGHT (+/-)
static float gyro_rollright=0, gyro_rollleft=0;   // about trailer FORWARD (RIGHT = positive)
static float gyro_turnright=0, gyro_turnleft=0;   // about trailer UP

// EMA for Leveling (display only)
static float pos_pitch_avg=0, pos_roll_avg=0;
static uint32_t lastAvgMs=0; static bool avgInit=false;

// Gravity magnitude (in g), measured at Calibrate; default if unset
static float g_mag = TL_GRAVITY_G_DEFAULT;

// Gravity components (debug)
static float g_fwd_sub=0, g_rgt_sub=0, g_up_sub=0;

static float finiteOr(float v, float fb=0.0f){ return isfinite(v)?v:fb; }

// -------- Peak-hold structures (with decay) --------
struct Peak4 { float up=0, down=0, left=0, right=0; };
static Peak4 g_accelPeak;      // in g (directional)
static Peak4 g_rollPeak;       // in dps (directional)
static uint32_t accelPeakLastMs=0, rollPeakLastMs=0;

static void updatePeak(Peak4& peak, const Peak4& nowvals, uint32_t& lastMs, float tau_ms){
  uint32_t now = millis();
  if (lastMs == 0) {
    peak = nowvals;
    lastMs = now;
    return;
  }
  uint32_t dt = now - lastMs; if (dt > 2000) dt = 2000;
  float alpha = 1.0f - expf(-(float)dt / tau_ms);
  auto step = [&](float cur, float p)->float{
    if (cur > p) return cur;               // immediate rise to new peak
    return p + alpha * (cur - p);          // decay toward current
  };
  peak.up    = step(nowvals.up,    peak.up);
  peak.down  = step(nowvals.down,  peak.down);
  peak.left  = step(nowvals.left,  peak.left);
  peak.right = step(nowvals.right, peak.right);
  lastMs = now;
}

// ---------------- Low-level I2C helpers ----------------
static bool i2cRead(uint8_t dev, uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(dev);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t n = Wire.requestFrom(dev, (uint8_t)len);
  if (n != len) return false;
  for (size_t i=0;i<len;++i) buf[i]=Wire.read();
  return true;
}
static bool readGyroRawRegs(int16_t& rx, int16_t& ry, int16_t& rz) {
  uint8_t b[6];
  if (!i2cRead(MPU_ADDR, 0x43, b, 6)) return false;
  rx = (int16_t)((b[0]<<8)|b[1]); ry=(int16_t)((b[2]<<8)|b[3]); rz=(int16_t)((b[4]<<8)|b[5]);
  return true;
}
static float gyroLSBperDPS() {
  uint8_t cfg=0; if (!i2cRead(MPU_ADDR, 0x1B, &cfg, 1)) return 131.0f;
  switch ((cfg>>3)&0x03) { case 0: return 131.0f; case 1: return 65.5f; case 2: return 32.8f; case 3: return 16.4f; }
  return 131.0f;
}

// ---------------- Preferences (basis + calibration) ----------------
static void saveBasis(const float up_s[3]){
  prefs.begin("ori2", false);
  prefs.putFloat("upx", up_s[0]); prefs.putFloat("upy", up_s[1]); prefs.putFloat("upz", up_s[2]);
  prefs.putString("hint", g_forwardHint);
  prefs.putFloat("fx", g_basis.fwd[0]); prefs.putFloat("fy", g_basis.fwd[1]); prefs.putFloat("fz", g_basis.fwd[2]);
  prefs.putFloat("rx", g_basis.rgt[0]); prefs.putFloat("ry", g_basis.rgt[1]); prefs.putFloat("rz", g_basis.rgt[2]);
  prefs.putFloat("ux", g_basis.up[0]);  prefs.putFloat("uy", g_basis.up[1]);  prefs.putFloat("uz", g_basis.up[2]);
  prefs.end();
}
static bool loadBasis(float up_out[3]){
  prefs.begin("ori2", true);
  bool has = prefs.isKey("upx") && prefs.isKey("ux") && prefs.isKey("fx");
  if (has){
    up_out[0]=prefs.getFloat("upx",0); up_out[1]=prefs.getFloat("upy",0); up_out[2]=prefs.getFloat("upz",1);
    g_forwardHint=prefs.getString("hint","+X");
    g_basis.fwd[0]=prefs.getFloat("fx",1); g_basis.fwd[1]=prefs.getFloat("fy",0); g_basis.fwd[2]=prefs.getFloat("fz",0);
    g_basis.rgt[0]=prefs.getFloat("rx",0); g_basis.rgt[1]=prefs.getFloat("ry",1); g_basis.rgt[2]=prefs.getFloat("rz",0);
    g_basis.up[0] =prefs.getFloat("ux",0); g_basis.up[1] =prefs.getFloat("uy",0); g_basis.up[2] =prefs.getFloat("uz",1);
    g_basis.valid=true;
  }
  prefs.end();
  return has;
}
static void saveCalibration() {
  prefs.begin("imu", false);
  prefs.putFloat("pitch_zero", pos_pitch_zero);
  prefs.putFloat("roll_zero",  pos_roll_zero);
  prefs.putFloat("g_mag",      g_mag);
  prefs.end();
}
static void loadOrBootstrapCalibration() {
  prefs.begin("imu", true);
  bool hasPitch = prefs.isKey("pitch_zero") || prefs.isKey("pitch_off");
  bool hasRoll  = prefs.isKey("roll_zero")  || prefs.isKey("roll_off");
  g_mag = prefs.getFloat("g_mag", TL_GRAVITY_G_DEFAULT);
  if (hasPitch && hasRoll) {
    pos_pitch_zero = prefs.getFloat(prefs.isKey("pitch_zero") ? "pitch_zero" : "pitch_off", 0.0f);
    pos_roll_zero  = prefs.getFloat(prefs.isKey("roll_zero")  ? "roll_zero"  : "roll_off",  0.0f);
    prefs.end(); return;
  }
  prefs.end();

  for (int i=0;i<30;i++){ mpu.update(); delay(5); }
  float ax=mpu.getAccX(), ay=mpu.getAccY(), az=mpu.getAccZ();
  float fwdPose,rgtPose,upPose; toTrailer(ax,ay,az,fwdPose,rgtPose,upPose);
  float denom = sqrtf(rgtPose*rgtPose + upPose*upPose); if (denom < 1e-6f) denom = 1e-6f;
  pos_pitch_zero = atan2f(-fwdPose, denom) * 180.0f / PI;
  pos_roll_zero  = atan2f( rgtPose, upPose ) * 180.0f / PI;
  saveCalibration();
}

// ---------------- Sensor read and derive values ----------------
static void readIMU() {
  mpu.update();
  accel_x_raw=mpu.getAccX(); accel_y_raw=mpu.getAccY(); accel_z_raw=mpu.getAccZ();
  gyro_x_raw=mpu.getGyroX(); gyro_y_raw=mpu.getGyroY(); gyro_z_raw=mpu.getGyroZ();

  if (!isfinite(gyro_x_raw) || !isfinite(gyro_y_raw) || !isfinite(gyro_z_raw)) {
    int16_t grx,gry,grz;
    if (readGyroRawRegs(grx,gry,grz)) {
      float lsb=gyroLSBperDPS(); gyro_x_raw=(float)grx/lsb; gyro_y_raw=(float)gry/lsb; gyro_z_raw=(float)grz/lsb;
    } else { gyro_x_raw=gyro_y_raw=gyro_z_raw=0; }
  }

  // Map accel into trailer frame (raw)
  float af_raw, ar_raw, au_raw;
  toTrailer(accel_x_raw, accel_y_raw, accel_z_raw, af_raw, ar_raw, au_raw);

  // Pose from oriented raw accel (not gravity-removed)
  float denom = sqrtf(ar_raw*ar_raw + au_raw*au_raw); if (denom < 1e-6f) denom=1e-6f;
  pos_pitch_raw = atan2f(-af_raw, denom) * 180.0f / PI;
  pos_roll_raw  = atan2f( ar_raw, au_raw ) * 180.0f / PI;

  auto wrap180=[](float a){ if(!isfinite(a))return 0.0f; while(a>180.0f)a-=360.0f; while(a<-180.0f)a+=360.0f; return a; };
  pos_pitch_calibrated = wrap180(pos_pitch_raw - pos_pitch_zero);
  pos_roll_calibrated  = wrap180(pos_roll_raw  - pos_roll_zero);

  // EMA for Leveling (display only)
  uint32_t now = millis();
  if (!avgInit) { pos_pitch_avg=pos_pitch_calibrated; pos_roll_avg=pos_roll_calibrated; avgInit=true; lastAvgMs=now; }
  else {
    uint32_t dt = now - lastAvgMs; if (dt>2000) dt=2000;
    float alpha = 1.0f - expf(-(float)dt / (float)TL_LEVEL_AVG_TAU_MS);
    if (alpha<0) alpha=0; if (alpha>1) alpha=1;
    pos_pitch_avg += alpha*(pos_pitch_calibrated - pos_pitch_avg);
    pos_roll_avg  += alpha*(pos_roll_calibrated  - pos_roll_avg);
    lastAvgMs=now;
  }

  // Gravity removal using instantaneous Level ONLY
  float pr = pos_pitch_calibrated * (PI/180.0f);
  float rr = pos_roll_calibrated  * (PI/180.0f);
  g_fwd_sub = -sinf(pr) * g_mag;
  g_rgt_sub =  sinf(rr) * g_mag;
  g_up_sub  =  cosf(pr) * cosf(rr) * g_mag;

  auto dz = [](float v){ return (fabsf(v) < TL_ACCEL_DEADBAND_G) ? 0.0f : v; };
  float af = dz(af_raw - g_fwd_sub);
  float ar = dz(ar_raw - g_rgt_sub);
  float au = dz(au_raw - g_up_sub);

  accel_forward = af; accel_backward = -af;
  accel_right   = ar; accel_left     = -ar;
  accel_up      = au; accel_down     = -au;

  // Gyro vector → trailer frame (RIGHT positive per convention)
  float gf, gr, gu; toTrailer(gyro_x_raw, gyro_y_raw, gyro_z_raw, gf, gr, gu);
  gyro_pitchup    = max(0.0f, +gr);
  gyro_pitchdown  = max(0.0f, -gr);
  gyro_rollright  = max(0.0f, -gf);  // RIGHT positive
  gyro_rollleft   = max(0.0f, +gf);
  gyro_turnright  = max(0.0f, +gu);
  gyro_turnleft   = max(0.0f, -gu);

  // -------- Update peak-hold (directional) with decay --------
  // Accel currents per direction (non-negative)
  Peak4 accNow;
  accNow.up    = max(0.0f,  accel_forward);
  accNow.down  = max(0.0f, -accel_forward);
  accNow.right = max(0.0f,  accel_right);
  accNow.left  = max(0.0f, -accel_right);
  updatePeak(g_accelPeak, accNow, accelPeakLastMs, (float)TL_ACCEL_PEAK_TAU_MS);

  // Roll currents per direction (non-negative)
  Peak4 rollNow;
  rollNow.up    = gyro_pitchup;
  rollNow.down  = gyro_pitchdown;
  rollNow.right = gyro_rollright;   // RIGHT positive
  rollNow.left  = gyro_rollleft;
  updatePeak(g_rollPeak, rollNow, rollPeakLastMs, (float)TL_ROLL_PEAK_TAU_MS);
}

// ---------------- HTTP helpers & captive portal ----------------
static void addCORS(){ server.sendHeader("Access-Control-Allow-Origin","*"); server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS"); server.sendHeader("Access-Control-Allow-Headers","Content-Type"); server.sendHeader("Cache-Control","no-store"); }
static void sendJson(int code, const String& body){ addCORS(); server.send(code,"application/json",body); }

static String hostUrl(const char* path){
  String u = "http://"; u += TL_DOMAIN;
  if (TL_HTTP_PORT!=80){ u += ":"; u += String(TL_HTTP_PORT); }
  if (path && *path) { if (path[0]!='/') u+='/'; u+=path; }
  return u;
}
static String mdnsHostLabel(){ String s=TL_DOMAIN; s.toLowerCase(); int dot=s.indexOf('.'); if(dot>0) s.remove(dot); return s; }

static void startCaptiveDNS() {
  apIP = WiFi.softAPIP();
  dnsServer.stop();
  dnsServer.setTTL(TL_DNS_TTL_SECONDS);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
}
static void captiveHtml200() {
  String ui = hostUrl(TL_WEB_UI_PATH);
  server.send(200, "text/html",
    String(F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<title>Trailer Level</title>"
             "<p>Opening UI… If not, <a href='")) + ui + F("'>tap here</a>."
             "<script>location.replace('") + ui + F("')</script>"));
}
static void registerCaptiveRoutes() {
  server.on("/", HTTP_GET, captiveHtml200);
  // Android
  server.on("/generate_204", HTTP_GET, captiveHtml200);
  server.on("/gen_204", HTTP_GET, captiveHtml200);
  server.on("/google/generate_204", HTTP_GET, captiveHtml200);
  server.on("/connectivity-check", HTTP_GET, captiveHtml200);
  server.on("/connectivitycheck.gstatic.com/generate_204", HTTP_GET, captiveHtml200);
  // Apple
  server.on("/hotspot-detect.html", HTTP_GET, captiveHtml200);
  server.on("/success.html", HTTP_GET, captiveHtml200);
  server.on("/library/test/success.html", HTTP_GET, captiveHtml200);
  server.on("/captive.apple.com", HTTP_GET, captiveHtml200);
  // Windows
  server.on("/ncsi.txt", HTTP_GET, captiveHtml200);
  server.on("/connecttest.txt", HTTP_GET, captiveHtml200);
  server.on("/www.msftconnecttest.com/connecttest.txt", HTTP_GET, captiveHtml200);
  // Chrome / misc
  server.on("/canonical.html", HTTP_GET, captiveHtml200);

  server.onNotFound(captiveHtml200);
}

// ---------------- HTTP handlers ----------------
static void handleSensor() {
  readIMU();
  JsonDocument doc;

  // Pose (instant + averaged)
  doc["pos_pitch_raw"]        = pos_pitch_raw;
  doc["pos_roll_raw"]         = pos_roll_raw;
  doc["pos_pitch_calibrated"] = pos_pitch_calibrated;
  doc["pos_roll_calibrated"]  = pos_roll_calibrated;
  doc["pos_pitch_avg"]        = pos_pitch_avg;
  doc["pos_roll_avg"]         = pos_roll_avg;

  // Raw sensor
  doc["accel_x_raw"]=accel_x_raw; doc["accel_y_raw"]=accel_y_raw; doc["accel_z_raw"]=accel_z_raw;
  doc["gyro_x_raw"]=gyro_x_raw;   doc["gyro_y_raw"]=gyro_y_raw;   doc["gyro_z_raw"]=gyro_z_raw;

  // Accel (gravity removed) in trailer frame (current)
  doc["accel_forward"]=accel_forward; doc["accel_backward"]=accel_backward;
  doc["accel_right"]=accel_right;     doc["accel_left"]=accel_left;
  doc["accel_up"]=accel_up;           doc["accel_down"]=accel_down;

  // Accel peaks (directional)
  {
    JsonObject ap = doc["accel_peak"].to<JsonObject>();
    ap["up"]    = g_accelPeak.up;
    ap["down"]  = g_accelPeak.down;
    ap["left"]  = g_accelPeak.left;
    ap["right"] = g_accelPeak.right;
  }

  // Gravity components we subtracted (for debugging)
  doc["gravity_forward"]=g_fwd_sub;
  doc["gravity_right"]=g_rgt_sub;
  doc["gravity_up"]=g_up_sub;

  // Gyro split (current directional magnitudes)
  doc["gyro_pitchup"]=gyro_pitchup;     doc["gyro_pitchdown"]=gyro_pitchdown;
  doc["gyro_rollright"]=gyro_rollright; doc["gyro_rollleft"]=gyro_rollleft;
  doc["gyro_turnright"]=gyro_turnright; doc["gyro_turnleft"]=gyro_turnleft;

  // Roll peaks (directional)
  {
    JsonObject rp = doc["roll_peak"].to<JsonObject>();
    rp["up"]    = g_rollPeak.up;
    rp["down"]  = g_rollPeak.down;
    rp["left"]  = g_rollPeak.left;
    rp["right"] = g_rollPeak.right;
  }

  String json; serializeJson(doc, json); sendJson(200, json);
}

static void handleCalibrate() {
  for (int i=0;i<40;i++){ mpu.update(); delay(4); }

  const int N=80;
  float ax=0,ay=0,az=0, gsum=0;
  for (int i=0;i<N;i++){
    mpu.update();
    float x=mpu.getAccX(), y=mpu.getAccY(), z=mpu.getAccZ();
    ax+=x; ay+=y; az+=z; gsum += sqrtf(x*x + y*y + z*z);
    delay(2);
  }
  ax/=N; ay/=N; az/=N;
  g_mag = gsum / N;

  float up_s[3] = { ax, ay, az }; normalize3(up_s);
  buildBasisFromUpAndHint(up_s); saveBasis(up_s);

  float fwdPose,rgtPose,upPose; toTrailer(ax, ay, az, fwdPose, rgtPose, upPose);
  float denom = sqrtf(rgtPose*rgtPose + upPose*upPose); if (denom < 1e-6f) denom=1e-6f;
  pos_pitch_zero = atan2f(-fwdPose, denom) * 180.0f / PI;
  pos_roll_zero  = atan2f( rgtPose, upPose ) * 180.0f / PI;
  saveCalibration();

  // Reset peak-hold so they start fresh
  g_accelPeak = Peak4{}; accelPeakLastMs = millis();
  g_rollPeak  = Peak4{}; rollPeakLastMs  = millis();

  // Reset smoothing
  pos_pitch_avg = pos_roll_avg = 0.0f; avgInit=true; lastAvgMs=millis();

  JsonDocument resp; resp["status"]="ok"; resp["forward_hint"]=g_forwardHint; resp["g_mag"]=g_mag;
  String json; serializeJson(resp, json); sendJson(200, json);
}

static void handleGetCalibration() {
  JsonDocument d; d["pos_pitch_zero"]=pos_pitch_zero; d["pos_roll_zero"]=pos_roll_zero; d["g_mag"]=g_mag;
  String json; serializeJson(d, json); sendJson(200, json);
}
static void handleResetCalibration() {
  pos_pitch_zero=0; pos_roll_zero=0; g_mag=TL_GRAVITY_G_DEFAULT; saveCalibration();
  pos_pitch_avg=pos_roll_avg=0; avgInit=true; lastAvgMs=millis();
  g_accelPeak = Peak4{}; g_rollPeak = Peak4{}; accelPeakLastMs=rollPeakLastMs=millis();
  sendJson(200, "{\"status\":\"ok\"}");
}

// GET: basis info; POST: set forward_hint=+X|-X|+Y|-Y and rebuild basis using saved UP
static void handleOrientation() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    doc["mode"] = g_basis.valid ? "basis" : "unset";
    doc["forward_hint"] = g_forwardHint;

    JsonObject basis = doc["basis"].to<JsonObject>();
    JsonArray f = basis["forward"].to<JsonArray>(); f.add(g_basis.fwd[0]); f.add(g_basis.fwd[1]); f.add(g_basis.fwd[2]);
    JsonArray r = basis["right"].to<JsonArray>();   r.add(g_basis.rgt[0]); r.add(g_basis.rgt[1]); r.add(g_basis.rgt[2]);
    JsonArray u = basis["up"].to<JsonArray>();      u.add(g_basis.up[0]);  u.add(g_basis.up[1]);  u.add(g_basis.up[2]);

    String json; serializeJson(doc, json); sendJson(200, json);
    return;
  }

  if (server.method() == HTTP_POST) {
    String hint = g_forwardHint;

    if (server.hasArg("plain") && server.arg("plain").length() > 0) {
      JsonDocument body;
      if (!deserializeJson(body, server.arg("plain"))) {
        if (body["forward_hint"].is<const char*>()) {
          hint = String((const char*)body["forward_hint"]);
        }
      }
    }

    hint.trim(); hint.toUpperCase();
    if (!(hint == "+X" || hint == "-X" || hint == "+Y" || hint == "-Y")) {
      sendJson(400, "{\"error\":\"forward_hint must be +X|-X|+Y|-Y\"}");
      return;
    }
    g_forwardHint = hint;

    prefs.begin("ori2", true);
    float up_s[3] = { prefs.getFloat("upx", 0), prefs.getFloat("upy", 0), prefs.getFloat("upz", 1) };
    prefs.end();
    if (norm3(up_s) < 1e-6f) { mpu.update(); up_s[0]=mpu.getAccX(); up_s[1]=mpu.getAccY(); up_s[2]=mpu.getAccZ(); }
    normalize3(up_s);

    buildBasisFromUpAndHint(up_s);
    saveBasis(up_s);

    sendJson(200, String("{\"status\":\"ok\",\"forward_hint\":\"") + g_forwardHint + "\"}");
    return;
  }

  sendJson(405, "{\"error\":\"method not allowed\"}");
}

// /wifi (POST)
static bool asciiPrintable(const String& s){ for (size_t i=0;i<s.length();++i){ char c=s[i]; if(c<0x20||c>0x7E) return false; } return true; }
static void handleWifiUpdate() {
  addCORS();
  if (!server.hasArg("plain") || server.arg("plain").isEmpty()){
    server.send(400,"application/json","{\"error\":\"no body\"}");
    return;
  }
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain"))) {
    server.send(400,"application/json","{\"error\":\"bad json\"}");
    return;
  }
  String newSsid = String(body["ssid"]|""); String newPwd  = String(body["password"]|""); newSsid.trim();
  if (newSsid.length()<1 || newSsid.length()>32 || !asciiPrintable(newSsid)){
    server.send(400,"application/json","{\"error\":\"ssid must be 1..32 printable ASCII\"}");
    return;
  }
  if (newPwd.length()>0 && (newPwd.length()<8 || !asciiPrintable(newPwd))){
    server.send(400,"application/json","{\"error\":\"password must be >=8 printable ASCII or empty\"}");
    return;
  }
  prefs.begin("wifi", false); prefs.putString("ssid", newSsid); prefs.putString("password", newPwd); prefs.end();

  // Schedule hot-apply (uses global g_wifiPending)
  g_wifiPending.apply    = true;
  g_wifiPending.ssid     = newSsid;
  g_wifiPending.password = newPwd;
  g_wifiPending.at_ms    = millis() + 500;

  String resp = String("{\"status\":\"ok\",\"ssid\":\"") + newSsid + "\"}";
  server.send(200,"application/json",resp);
}

// ---------------- Wi-Fi / mDNS / AP -----------------------------------------
static void setupWifiEvents() {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    DEBUG_PRINT("AP client connected AID="); DEBUG_PRINTLN(info.wifi_ap_staconnected.aid);
  }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    DEBUG_PRINT("AP client disconnected AID="); DEBUG_PRINTLN(info.wifi_ap_stadisconnected.aid);
  }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
}
static bool startMDNSHost(){
  String label = mdnsHostLabel();
  if (MDNS.begin(label.c_str())) {
    MDNS.addService("http","tcp",TL_HTTP_PORT);
    DEBUG_PRINT("mDNS up: http://"); DEBUG_PRINT(label); DEBUG_PRINTLN(".local");
    return true;
  }
  DEBUG_PRINTLN("mDNS failed"); return false;
}
static bool startAP(const String& ssid, const String& pwd) {
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(TL_AP_TX_POWER);

  IPAddress ip, gw, mask;
  ip.fromString(TL_AP_IP);
  gw.fromString(TL_AP_GATEWAY);
  mask.fromString(TL_AP_NETMASK);
  WiFi.softAPConfig(ip, gw, mask);

  bool ok = (pwd.length()==0) ? WiFi.softAP(ssid.c_str()) : WiFi.softAP(ssid.c_str(), pwd.c_str());
  apIP = WiFi.softAPIP();
  DEBUG_PRINT("softAP("); DEBUG_PRINT(ssid); DEBUG_PRINT(", ****) -> "); DEBUG_PRINTLN(ok?"OK":"FAIL");
  DEBUG_PRINT("AP IP address: "); DEBUG_PRINTLN(apIP);

  if (ok) { startCaptiveDNS(); startMDNSHost(); }
  return ok;
}
static void setupWifi() {
  setupWifiEvents();
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", TL_DEFAULT_SSID);
  String password = prefs.getString("password", TL_DEFAULT_PASSWORD);
  prefs.end();
  startAP(ssid, password);
}

// ---------------- IMU bring-up ----------------
static bool initIMU() {
  Wire.setClock(400000);
  Wire.setTimeOut(1000);
  byte status = mpu.begin(MPU_ADDR);
  if (status != 0) { DEBUG_PRINTLN("MPU begin failed"); return false; }
  for (int i=0;i<30;i++){ mpu.update(); delay(5); }
  return true;
}

// ---------------- Arduino ----------------
void setup() {
#ifdef DEBUG_SERIAL
  Serial.begin(115200); delay(200);
#endif
  Wire.begin(TL_I2C_SDA_PIN, TL_I2C_SCL_PIN); delay(10);
  initIMU();

  float up_loaded[3];
  if (!loadBasis(up_loaded)) {
    for (int i=0;i<30;i++){ mpu.update(); delay(5); }
    float up_s[3] = { mpu.getAccX(), mpu.getAccY(), mpu.getAccZ() };
    normalize3(up_s); buildBasisFromUpAndHint(up_s); saveBasis(up_s);
  }

  loadOrBootstrapCalibration();
  setupWifi();

  // API routes
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/calibration", HTTP_GET, handleGetCalibration);
  server.on("/calibration/reset", HTTP_POST, handleResetCalibration);
  server.on("/orientation", HTTP_GET, handleOrientation);
  server.on("/orientation", HTTP_POST, handleOrientation);
  server.on("/wifi", HTTP_POST, handleWifiUpdate);

  // Web UI
  server.on(TL_WEB_UI_PATH, HTTP_GET, [=](){ server.send_P(200, "text/html", INDEX_HTML); });

  // CORS preflight
  server.on("/sensor", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/calibrate", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/calibration", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/calibration/reset", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/orientation", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/wifi", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });

  // Captive portal
  registerCaptiveRoutes();

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  mpu.update();

  // Apply pending Wi-Fi change after response has been sent
  if (g_wifiPending.apply && (int32_t)(millis()-g_wifiPending.at_ms)>=0){
    g_wifiPending.apply=false;
    WiFi.softAPdisconnect(true); delay(150);
    if (!startAP(g_wifiPending.ssid, g_wifiPending.password)) {
      WiFi.softAPdisconnect(true); delay(150);
      startAP(TL_DEFAULT_SSID, TL_DEFAULT_PASSWORD);
    }
  }
}
