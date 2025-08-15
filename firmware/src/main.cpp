#include <Arduino.h>
#include <Wire.h>
#include <MPU9250.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"

#ifdef DEBUG_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

MPU9250 imu;
WebServer server(80);
Preferences prefs;

float pitch = 0, roll = 0, heading = 0;

void readIMU() {
  imu.update();
  float ax = imu.getAccX();
  float ay = imu.getAccY();
  float az = imu.getAccZ();
  float mx = imu.getMagX();
  float my = imu.getMagY();
  pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  roll = atan2(ay, az) * 180.0 / PI;
  heading = atan2(my, mx) * 180.0 / PI;
  if (heading < 0) heading += 360.0;
  DEBUG_PRINT("IMU: ax="); DEBUG_PRINT(ax);
  DEBUG_PRINT(", ay="); DEBUG_PRINT(ay);
  DEBUG_PRINT(", az="); DEBUG_PRINT(az);
  DEBUG_PRINT(", pitch="); DEBUG_PRINT(pitch);
  DEBUG_PRINT(", roll="); DEBUG_PRINT(roll);
  DEBUG_PRINT(", heading="); DEBUG_PRINTLN(heading);
}

void handleSensor() {
  DEBUG_PRINTLN("/sensor requested");
  readIMU();
  JsonDocument doc;
  doc["pitch"] = pitch;
  doc["roll"] = roll;
  doc["heading"] = heading;
  String json;
  serializeJson(doc, json);
  DEBUG_PRINTLN(json);
  server.send(200, "application/json", json);
}

void handleWifi() {
  DEBUG_PRINTLN("/wifi requested");
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char* ssid = doc["ssid"];
    const char* password = doc["password"];
    DEBUG_PRINT("Updating credentials to SSID: ");
    DEBUG_PRINTLN(ssid);
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
  }
}

void setupWifi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", DEFAULT_SSID);
  String password = prefs.getString("password", DEFAULT_PASSWORD);
  prefs.end();

  DEBUG_PRINT("Starting AP: "); DEBUG_PRINTLN(ssid);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), password.c_str());
  DEBUG_PRINT("AP IP address: "); DEBUG_PRINTLN(WiFi.softAPIP());
}

void setup() {
#ifdef DEBUG_SERIAL
  Serial.begin(115200);
  DEBUG_PRINTLN("Serial debug enabled");
#endif
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!imu.setup(0x68)) {
    DEBUG_PRINTLN("IMU initialization unsuccessful");
    while (1) {
      delay(1000);
    }
  }
  setupWifi();
  server.on("/sensor", handleSensor);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.begin();
  DEBUG_PRINTLN("HTTP server started");
}

void loop() {
  server.handleClient();
}
