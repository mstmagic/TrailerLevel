# TrailerLevel — Build and Assembly Guide

This README walks you from 3D printing the enclosure to flashing and assembling the ESP32-based sensor level.

Repository: https://github.com/mstmagic/TrailerLevel

---

## 1) Bill of Materials

**Electronics**
- 1 × ESP32-S2 Zero module (USB-C or Micro-USB, depending on your board)
- 1 × IMU module (MPU-9250 or MPU-6050/6500, I²C)
- 1 × DC-DC buck converter (input from your trailer’s supply, regulated output to 5 V)
- 1 × Panel-mount barrel jack (to bring power to the buck converter input)
- Heat-shrink tubing sized for the buck converter
- Hookup wire:
  - Red for positive, white for ground, green for I²C lines (or your preferred colors)

**Hardware**
- 3D printed body and lid from the repo’s `3d models` folder
- M3 heat-set inserts:
  - **Short** M3 for the IMU mount
  - **Long** M3 for the lid standoffs
- M3 screws:
  - **Short** M3 for the IMU board
  - **Long** M3 for the lid
- 1 × small zip tie (to strap the ESP module at the printed tie slot)

**Tools**
- Soldering iron with a flat tip for heat-set inserts
- Standard soldering tools and flux
- Side cutters, wire strippers
- Heat gun or lighter for shrink tubing
- Screwdriver for M3 hardware
- Calipers or a ruler to measure wire lengths

---

## 2) 3D Print and Dry-Fit

1. **Print the housing and lid** from the `3d models` folder. PETG or ABS is recommended for temperature resilience. Suggested settings:
   - 0.2 mm layer height
   - 3 walls
   - 20 to 40 percent infill

2. **Dry-fit the boards**:
   - Place the ESP32-S2 Zero in its bay. Confirm the USB connector has clearance.
   - Place the IMU in its pocket. **Note sensor orientation**. The IMU’s axes must align with the body as intended by the firmware. Mark the board orientation so you install it the same way after soldering.

3. **Plan wire lengths**:
   - Leads must be **short** to fit inside the enclosure without stress. Route the wires along their intended paths and measure. Cut a little long, then trim to final length during soldering.

---

## 3) Heat-Set Inserts

1. **Short M3 inserts** into the IMU mount:
   - Use the soldering iron to press the **short** inserts into the IMU mounting holes in the printed pocket.
   - Keep them square and flush.

2. **Long M3 inserts** for the lid standoffs:
   - Press the **long** inserts into the four lid post holes in the body.

Let the plastic cool before handling.

---

## 4) Firmware Setup

### 4.1 Install VS Code

- Download and install Visual Studio Code from the official website for your OS.

### 4.2 Install PlatformIO in VS Code

1. Open VS Code.
2. Go to **Extensions** (Ctrl+Shift+X).
3. Search for **PlatformIO IDE** and click **Install**.
4. Restart VS Code if prompted.

### 4.3 Clone the Repository

Use either the command line or VS Code’s built-in Git:

```bash
# Command line example
git clone https://github.com/mstmagic/TrailerLevel.git
cd TrailerLevel/firmware
