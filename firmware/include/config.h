#pragma once

// ----------------------- Build / Debug ---------------------------------------
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL
#endif

// ----------------------- Network & Captive Portal ----------------------------
// Full dotted domain used in links and captive-portal redirects
// Example: "trailer.local"
#define TL_DOMAIN                "trailer.local"

// Path where the web app is served
#define TL_WEB_UI_PATH           "/ui"

// SoftAP network configuration (dotted strings)
#define TL_AP_IP                 "192.168.4.1"
#define TL_AP_GATEWAY            "192.168.4.1"
#define TL_AP_NETMASK            "255.255.255.0"

// DNS captive portal TTL (seconds)
#define TL_DNS_TTL_SECONDS       1

// HTTP port for the built-in server
#define TL_HTTP_PORT             80

// SoftAP transmit power (see WiFi.h: WIFI_POWER_* constants)
#define TL_AP_TX_POWER           WIFI_POWER_8_5dBm

// Default Wi-Fi credentials (used on first boot or if not set via /wifi)
#define TL_DEFAULT_SSID          "TrailerLevel"
#define TL_DEFAULT_PASSWORD      "password"

// ----------------------- Sensors & UI ----------------------------------------
// I2C pins for MPU-6050/6500/9250 family
#define TL_I2C_SDA_PIN           13
#define TL_I2C_SCL_PIN           12

// Running average time constant (ms) for Leveling gauge (EMA - display only)
#define TL_LEVEL_AVG_TAU_MS      600

// Default gravity in g if not yet calibrated
#define TL_GRAVITY_G_DEFAULT     1.0f

// Per-axis deadband after gravity removal (in g). Lower = more sensitive.
#define TL_ACCEL_DEADBAND_G      0.0f

// Default UI polling period hint (ms) – UI can override via ?ms=
#define TL_UI_DEFAULT_POLL_MS    200

// ----------------------- Peak-Hold (Decay) -----------------------------------
// Time constants (ms) for exponential decay of peak-hold indicators
// Larger = slower decay; Smaller = faster decay
#define TL_ACCEL_PEAK_TAU_MS     1000   // Acceleration gauge peak decay
#define TL_ROLL_PEAK_TAU_MS      1000   // Roll (gyro) gauge peak decay
