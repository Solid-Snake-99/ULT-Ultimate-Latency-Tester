# ULT: Ultimate Latency Tester (DirectX 12)

<p align="center">
  <strong>Ultra High-Performance Hardware Controller Latency & Input Lag Benchmark for Windows</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/DirectX-12-0078D4?style=for-the-badge&logo=windows&logoColor=white" alt="DirectX 12" />
  <img src="https://img.shields.io/badge/C%2B%2B-17%20MSVC-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/Polling%20Rate-1000Hz%20%2F%202000Hz-00E5FF?style=for-the-badge" alt="Polling Rate" />
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20(x64)-00D26A?style=for-the-badge" alt="Platform" />
</p>

---

## 📖 Overview

**ULT (Ultimate Latency Tester)** is a native **DirectX 12** Windows benchmark engineered to measure real hardware response time (Total Input Lag) of any gaming controller with sub-millisecond precision by tracking analog stick displacement from center to 90° right.

Featuring an ultra-low overhead procedural rendering pipeline and an asynchronous **2000Hz** input polling thread, ULT delivers extreme framerates (500+ FPS) to isolate hardware bottlenecks and benchmark pure input-to-display latency.

---

## ⚡ Key Features

* **Low-Latency DirectX 12 Engine**: Built with `DXGI_SWAP_EFFECT_FLIP_DISCARD` and DirectFlip swapchain to eliminate Desktop Window Manager (DWM) composition buffering.
* **Procedural Raymarched 3D Environment**: High-contrast perspective checkerboard floor, precision red vertical crosshair sightline with dynamic glow, and emerald-green 90° target line.
* **High-Precision Sub-Millisecond Stopwatch**: Driven by Windows `QueryPerformanceCounter` (QPC); triggers instantly on the first input past deadzone and locks time exactly at 90°.
* **Dedicated 2000Hz Input Polling Thread**: Asynchronous hardware HID packet acquisition completely decoupled from the render loop.
* **Zero-Stutter Asynchronous Device Watcher**: Hotplugging USB or Bluetooth controllers produces zero frame drops or rendering stutters.
* **Hardware Lightbar Control (DualShock 4 & DualSense 5)**: Automatically configures native Sony HID output reports to set electric cyan / neon blue lighting and player LEDs.
* **Modern Frosted Glass UI (Dark Acrylic)**: Segmented option pill chips, real-time live telemetry badges, and interactive language toggles (Italian 🇮🇹 / English 🇬🇧).

---

## 📊 Real-Time Measured Telemetry

| Metric | Description |
| :--- | :--- |
| **D3D12 FPS & Frametime** | Real-time rendering framerate and GPU execution time. |
| **USB/BT Polling Rate** | Measured controller packet transmission frequency in Hz (e.g., 250Hz, 500Hz, 1000Hz). |
| **Controller Input Delay** | Polling cycle delay calculated as $1000 / \text{Hz}$. |
| **D3D12 Render Latency** | Time taken by DirectX 12 to compose and submit the frame. |
| **Display Monitor Latency** | Average display scanout delay calculated from the active monitor's refresh rate (1000/(2×Hz)). |
| **Stick Hardware Latency** | Sensor delay (Potentiometer / Hall Effect / TMR), adjustable from 0 to 100 ms (measured via slow-mo camera). |
| **Jitter + Human Delay** | Human thumb actuation velocity deviation relative to theoretical digital limits. |
| **Best Record Time** | Lowest reaction time recorded during the active session. |

---

## 🎮 Supported Controllers by Input Mode

ULT supports gamepads connected via **USB Cable**, **Bluetooth**, or **Wireless Adapters**, organized across 4 input modes:

```
                              ┌─── 1. SONY RAWINPUT (1000Hz Native - DualShock 4 / DualSense 5)
                              ├─── 2. MICROSOFT XINPUT (Xbox Series X|S / Xbox One / Xbox 360)
  ULT INPUT SUBSYSTEM ────────┼─── 3. DIRECTINPUT 8 (Retro Gamepads, Arcade Sticks, Racing Wheels)
                              └─── 4. AUTO-DETECT (Automatically selects lowest latency backend)
```

### 1. 🔵 Sony Native RawInput (1000Hz HID)
*Bypasses intermediate OS abstraction layers to directly read 64-byte HID raw input reports at 1000Hz with RGB Lightbar control.*
* **Sony PlayStation DualSense 5** (USB & Bluetooth)
* **Sony PlayStation DualSense 5 Edge** (USB & Bluetooth)
* **Sony PlayStation DualShock 4 v1** (CUH-ZCT1, USB & Bluetooth)
* **Sony PlayStation DualShock 4 v2** (CUH-ZCT2, USB & Bluetooth)
* **Official Sony DualShock 4 USB Wireless Adapter**
* Custom and Pro-gaming controllers powered by Sony HID chipsets (Scuf Reflex, Nacon Revolution, Razer Raiju in PS4/PS5 mode).

### 2. 🟢 Microsoft XInput
*Low-overhead native integration for the Xbox ecosystem and Windows-compatible controllers.*
* **Xbox Wireless Controller (Series X\|S)** (USB, Bluetooth, Xbox Wireless Adapter)
* **Xbox Elite Wireless Controller Series 2 & Series 1**
* **Xbox One Wireless Controller** (Standard, S, X Revisions)
* **Xbox 360 Controller** (Wired & Wireless)
* All third-party XInput-compatible gamepads:
  * Scuf Instinct / Envision
  * Razer Wolverine (V2, V2 Pro, Chroma)
  * Thrustmaster eSwap X / S Pro
  * GameSir (G7 SE, T4k, Kaleid)
  * 8BitDo Ultimate (Xbox / 2.4G Edition)
  * PDP Rematch, Victrix Gambit, PowerA Fusion Pro, Flydigi Vader/Apex (in XInput mode).

### 3. 🟣 DirectInput 8 (Universal Fallback)
*Broad compatibility for legacy controllers, fightsticks, and simulation hardware.*
* Fightsticks & Arcade Sticks (Sanwa, Hori Fighting Commander, Qanba, Mayflash)
* Simulation Racing Wheels & Pedals (Logitech G29/G920/G923, Thrustmaster T300/TX, Fanatec)
* Generic USB / Bluetooth Gamepads (8BitDo in DInput mode, iPega, SteelSeries, etc.).

### 4. ⚡ Auto-Detect Mode (Recommended)
* Instantly detects connected devices and assigns the lowest-latency backend available (**Sony RawInput 1000Hz > XInput > DirectInput**).

---

## 🕹️ Controls & Keybindings

### Gamepad Controls:
* **`OPTIONS` / `START`**: Open / Close Settings Menu
* **`D-PAD (Up / Down)`**: Navigate menu rows
* **`D-PAD (Left / Right)`**: Modify selected option value
* **`✕` (PlayStation) / `A` (Xbox)**: Confirm selection
* **`◯` (PlayStation) / `B` (Xbox)**: Close menu / Cancel
* **`□` (PlayStation) / `X` (Xbox)**: Toggle active testing stick (**L3 Left** ⇄ **R3 Right**)
* **`△` (PlayStation) / `Y` (Xbox)**: Reset stopwatch timer and best record

### Keyboard Shortcuts:
* **`ESC`**: Open / Close Settings Menu
* **`Arrow Keys (↑ / ↓ / ← / →)`**: Navigate and modify options
* **`ENTER`**: Confirm selection
* **`Q`**: Toggle active testing stick (**L3** ⇄ **R3**)
* **`R`**: Reset stopwatch timer and best record
* **`0 - 9` (Numpad / Number keys)**: Direct typing of stick hardware latency (0 - 100 ms)
* **`F11`**: Toggle Fullscreen / Windowed Desktop mode

---

## ⚙️ Configuration Menu Options

1. **FPS Limit**: `30`, `60`, `120`, `144`, `180`, `240`, `500`, `Unlimited`
2. **Resolution**: `1080p`, `900p`, `720p`, `540p`
3. **Graphics Quality**: `Low (Ultra Fast)`, `Medium`, `High`
4. **Input Connection**: `Auto`, `DS4 Native`, `DS5 Native`, `XInput`, `DirectInput`
5. **Display Mode**: `Windowed (Desktop)`, `Exclusive Fullscreen (DirectFlip)`
6. **V-Sync**: `Disabled (Lowest Latency)`, `Enabled`
7. **Language**: `Italiano 🇮🇹`, `English 🇬🇧`
8. **Stick Response Curve**: `Linear (Analog)`, `Instant (Max Speed Digital)`
9. **Stick Hardware Latency**: Adjustable or directly typeable with number keys (0 - 100 ms)

---

## 🛠️ Building from Source

### Prerequisites:
* **Windows 10 / 11 64-bit**
* **Visual Studio 2019 / 2022** with *"Desktop development with C++"* workload (MSVC v142/v143 & Windows 10/11 SDK).

### One-Click Build:
1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/ULT-Ultimate-Latency-Tester.git
   cd ULT-Ultimate-Latency-Tester
   ```
2. Run the build script:
   ```bat
   build.bat
   ```
3. The standalone executable **`ULT Ultimate Latency Tester.exe`** will be generated and ready to run!

---

## 📄 License

This project is licensed under the **MIT License** (or chosen license). See the `LICENSE` file for details.

<p align="center">
  <em>Developed with passion for the esports, gaming, and controller hardware communities.</em>
</p>
