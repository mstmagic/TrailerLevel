# TrailerLevel — Build and Assembly Guide

Build, flash, and assemble the ESP32-based TrailerLevel sensor level. This guide follows the steps from 3D print to final assembly, with PlatformIO instructions included.

Repository: https://github.com/mstmagic/TrailerLevel

---

## Table of Contents
1. Bill of Materials
2. 3D Print and Dry Fit
3. Heat-Set Inserts
4. Software Setup
   - 4.1 Install Visual Studio Code
   - 4.2 Install PlatformIO
   - 4.3 Clone the Repository
   - 4.4 Open the Firmware in PlatformIO
   - 4.5 Configure `include/config.h`
5. Flash the ESP32-S2-Zero
6. Soldering and Wiring
7. Mechanical Assembly
8. First Power-On
9. Troubleshooting
10. Project Structure
11. Quick Commands
12. Safety Notes
13. License

---

## 1) Bill of Materials

**Electronics**
- ESP32-S2 Zero module
- IMU board (MPU-9250 or MPU-6050/6500 over I²C)
- DC-DC buck converter (vehicle/battery input to **regulated 5 V** output)
- Panel-mount barrel jack (power inlet)
- Heat-shrink tubing sized for the buck converter
- Hookup wire: red (V+), white (GND), green (I²C lines)

**Hardware**
- 3D-printed **body** and **lid** (from repo models)
- M3 heat-set threaded inserts  
  • **Short** M3 for the IMU mount  
  • **Long** M3 for the lid standoffs
- M3 screws  
  • **Short** M3 for the IMU  
  • **Long** M3 for the lid
- Small zip tie (to strap the ESP32 module)

**Tools**
- Soldering iron (standard tip for soldering, **flat tip** for heat-set inserts)
- Solder, flux, side cutters, wire strippers
- Heat gun or lighter for shrink tubing
- Screwdriver for M3 screws
- Calipers/ruler to measure wire lengths

---

## 2) 3D Print and Dry Fit

1. **Print the body and lid.** PETG or ABS recommended for temperature resilience.  
   Suggested: 0.2 mm layer height, 3 walls, 20–40% infill.
2. **Dry fit the boards.**  
   • Place the **ESP32-S2 Zero** in its bay; confirm USB clearance.  
   • Place the **IMU** in its pocket. **Note its axis orientation** and mark it. Correct orientation matters for level calculations.  
3. **Plan wire lengths.**  
   Leads must be **short** so the assembly fits and to keep I²C stable. Route the planned paths, measure, cut slightly long, and trim during soldering.

---

## 3) Heat-Set Inserts

Use a soldering iron to install threaded inserts into the printed body.

1. **IMU mount:** press **short** M3 inserts into the IMU pocket holes. Keep square and flush.  
2. **Lid standoffs:** press **long** M3 inserts into the four lid posts. Allow plastic to cool fully.

---

## 4) Software Setup

### 4.1 Install Visual Studio Code
Download and install VS Code for your OS.

### 4.2 Install PlatformIO
1. Open VS Code.  
2. Open **Extensions** (Ctrl+Shift+X).  
3. Search **PlatformIO IDE** and click **Install**.  
4. Restart VS Code if prompted.

### 4.3 Clone the Repository
Use VS Code Git or a terminal:
```bash
git clone https://github.com/mstmagic/TrailerLevel.git
cd TrailerLevel/firmware
```

### 4.4 Open the Firmware in PlatformIO
- In VS Code press **Ctrl+Shift+P** → **PlatformIO: Open Project**.  
- Select the `TrailerLevel/firmware` folder.  
- PlatformIO will install frameworks and libraries on first open.

### 4.5 Configure `include/config.h`
Open:
```
firmware/include/config.h
```
Edit at minimum:
- Wi‑Fi AP values:
  - `DEFAULT_SSID = "TrailerLevel"`
  - `DEFAULT_PASSWORD = "password"`
- I²C pins for ESP32-S2 Zero and your wiring:
  - `SDA_PIN = 13`
  - `SCL_PIN = 12`

Example:
```cpp
#pragma once

#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL
#endif

// Default WiFi credentials
const char* DEFAULT_SSID     = "TrailerLevel";
const char* DEFAULT_PASSWORD = "password";

// I2C pins for ESP32-S2 Zero
const int SDA_PIN = 13;
const int SCL_PIN = 12;
```

Adjust any other options in `config.h` as required by your branch and hardware.

---

## 5) Flash the ESP32-S2-Zero

> Power the board from **USB only** while flashing. Do not apply external 5 V at the same time.

1. Connect the ESP32-S2-Zero to your computer via USB.  
2. In VS Code, select the correct **Serial Port** in the bottom status bar if needed.  
3. **Build**: press **Ctrl+Shift+P** → `PlatformIO: Build`.  
4. **Upload**: press **Ctrl+Shift+P** → `PlatformIO: Upload`.

If your board needs manual bootloader entry: hold **BOOT**, tap **RESET**, release **BOOT**, then try **Upload** again.

Optional Serial Monitor: **Ctrl+Shift+P** → `PlatformIO: Monitor` (115200 baud unless project specifies otherwise).

---

## 6) Soldering and Wiring

Keep all leads **as short as practical** so the assembly fits and to reduce I²C noise.

**Power path (buck converter):**
- **BUCK Converter In+ (Red)** → leave a **long** lead. Do **not** solder to the barrel connector yet.  
- **BUCK Converter In− (White)** → leave a **long** lead. Do **not** solder to the barrel connector yet.  
- **BUCK Converter Out+ (Red)** → **ESP32 5V** and **MPU VCC**.  
- **BUCK Converter Out− (White)** → **ESP32 GND** and **MPU GND**.

**I²C lines:**
- **ESP32 GPIO 13 (Green)** → **MPU SDA**.  
- **ESP32 GPIO 12 (Green)** → **MPU SCL**.

Tips: pre‑tin pads and wire ends, use flux, and keep SDA/SCL similar length and away from noisy power leads.

**Seal the buck converter:** slide heat‑shrink over the buck and shrink it to protect against shorts.

---

## 7) Mechanical Assembly

- There is a hole in the 3D model where the ESP module sits. A zip tie can be pushed through that hole and it will pop out the other side.

Steps:
1. **Fasten the ESP module** with the zip tie. Do not overtighten.  
2. **Screw down the IMU** using the **short** M3 screws into the short inserts. Confirm orientation.  
3. **Prepare the barrel connector:** remove the **nut** and place it on the buck converter **input** leads (Red = In+, White = In−).  
4. **Thread the leads** through the body’s barrel‑jack hole.  
5. **Solder the barrel connector** to the leads and trim excess.  
6. **Secure the connector** by threading the nut onto the barrel connector to clamp it to the body.  
7. **Close the lid** using the **long** M3 screws into the long inserts.

---

## 8) First Power-On

1. Set the buck output to **5.0 V** before connecting to electronics.  
2. Power via the barrel connector. The ESP should boot.  
3. If the firmware provides an AP, connect to the SSID from `config.h` (default `TrailerLevel` with password `password`).  
4. Open the web UI if provided by your branch, or check output via the Serial Monitor.

---

## 9) Troubleshooting

- **No serial port:** try a different cable/port, install USB‑UART driver, or enter bootloader mode (hold BOOT, tap RESET).  
- **I²C device not found:** recheck SDA=GPIO13 and SCL=GPIO12, power and ground, and keep wires short. Some IMUs have address jumpers; default MPU address is often `0x68`.  
- **No web UI/AP:** verify SSID/password in `config.h` and read serial logs for errors.  
- **Power issues:** verify buck output is a stable 5 V before connecting boards. Do not power from USB and external 5 V simultaneously.

---

## 10) Project Structure

```
TrailerLevel/
├─ 3d models/               # printable body and lid
└─ firmware/                # PlatformIO project
   ├─ include/config.h      # user-editable settings
   ├─ src/                  # firmware sources
   └─ platformio.ini        # build environments
```

---

## 11) Quick Commands

From `TrailerLevel/firmware`:

```bash
# Build
pio run

# Upload
pio run -t upload

# Upload for a specific environment
pio run -e <env_name> -t upload

# Serial monitor (adjust baud if needed)
pio device monitor -b 115200
```

---

## 12) Safety Notes

- Double‑check polarity on the buck converter and barrel connector.  
- Verify 5 V output before connecting electronics.  
- Insulate exposed conductors with heat‑shrink.  
- Do not leave the buck converter uninsulated inside the enclosure.

---

## 13) License

See `LICENSE` in the repository.
