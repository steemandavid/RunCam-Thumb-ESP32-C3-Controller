# Functional Specification Document
# RunCam Thumb ESP32-C3 Controller

**Version:** 1.1
**Status:** Draft — protocol layer corrected against on-the-wire testing 2026-05-21
**Target Platform:** ESP32-C3 (Arduino framework)
**Camera:** RunCam Thumb Pro W

> **Change in v1.1:** §4 (RunCam Protocol Layer) has been rewritten to match
> the actual wire behaviour of the Thumb Pro W. Previous version (v1.0) was
> based on a draft of the protocol reference that turned out to be inaccurate
> in three places (frame format had a phantom LEN byte; responses described
> as headerless; CAMERA_CONTROL described as fire-and-forget). All three
> mistakes are corrected here. See `Serial_Diagnostic_Report.md` for the
> empirical evidence. Settings access (§4.5) is now marked as **deferred**
> pending vendor documentation.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Hardware Architecture](#2-hardware-architecture)
3. [Software Architecture](#3-software-architecture)
4. [RunCam Protocol Layer](#4-runcam-protocol-layer)
5. [Business Logic & State Machine](#5-business-logic--state-machine)
6. [WiFi & Web Server](#6-wifi--web-server)
7. [Web UI Specification](#7-web-ui-specification)
8. [REST API Specification](#8-rest-api-specification)
9. [WebSocket Protocol](#9-websocket-protocol)
10. [Configuration](#10-configuration)
11. [OLED Display](#11-oled-display)
12. [Settings Store](#12-settings-store)
13. [Testing Strategy](#13-testing-strategy)
14. [File & Directory Structure](#14-file--directory-structure)
15. [Dependencies](#15-dependencies)
16. [Implementation Notes for Coder LLM](#16-implementation-notes-for-coder-llm)

---

## 1. Project Overview

The **RunCam Thumb ESP32-C3 Controller** is an ESP32-C3-based embedded controller that interfaces a RunCam Thumb Pro 4K v2 action camera over UART using the RunCam Device Protocol (RCSP). The controller:

- Performs a **pre-flight configuration check** on power-up, verifying that the camera's resolution, FPS, and EIS settings match expected values before flight.
- **Auto-starts and auto-stops recording** based on an arming signal from a flight computer (Eggtimer Proton or equivalent).
- **Auto-stops recording after 5 minutes** to ensure proper SD card file closure, with an option to immediately restart.
- Exposes a **WiFi access point** and a **web-based UI** accessible from a smartphone, providing full camera control without access to physical camera buttons.
- Supports **camera configuration** (video, image, and feature settings) and **device information** retrieval over the web UI.

Folder name:  RunCam_Thumb_ESP32-C3_Controller


### 1.1 Design Principles

- **Hardware abstraction:** All hardware interactions (UART, GPIO) are accessed through interfaces so that a mock layer can be substituted for testing.
- **Testability first:** Business logic, protocol encoding, and state machine are fully unit-testable without physical hardware.
- **Simple and reliable:** The system prioritises predictable behaviour over feature richness, especially for flight-critical auto-record logic.
- **Coder-LLM friendly:** Code is structured in small, single-responsibility modules. Each module has accompanying unit tests. The implement-test-fix cycle can run autonomously on a Linux host.

---

## 2. Hardware Architecture

### 2.1 Components

| Component | Part |
|---|---|
| Microcontroller | ESP32-C3 OLED Development Board (ESP32-C3FN4/FH4, 25mm × 20.5mm) |
| Display | Built-in 0.42" SSD1306 OLED, 72×40 px, I2C hardwired to GPIO5/GPIO6 |
| Camera | RunCam Thumb Pro 4K v2 |
| Power supply | 1S LiPo (3.5–4.2 V) |
| Arming interface | Dedicated GPIO with pull-up (active low) |

### 2.2 Wiring

```
1S LiPo (+) ──┬──► RunCam JST (+)
              └──► ESP32-C3 5V pin (onboard LDO regulates to 3.3V)

1S LiPo (–) ──┬──► RunCam JST (–)
              └──► ESP32-C3 GND

RunCam TX   ──────► ESP32-C3 GPIO4   (UART1 RX)  [green wire]
RunCam RX   ◄──────  ESP32-C3 GPIO3   (UART1 TX)  [white wire]
RunCam GND  ──────► ESP32-C3 GND

ARM_SWITCH  ──────► ESP32-C3 GPIO2   (INPUT_PULLUP, active low)
```

**Notes:**
- The camera and ESP32-C3 share a common GND.
- The camera is powered directly from the LiPo; the ESP32-C3 is powered via its onboard LDO from the same LiPo.
- Both devices operate at 3.3 V logic. No level shifter is required.
- `GPIO_ARM` is pulled high internally. Connecting it to GND (via a flight computer arm output, relay, or manual switch) triggers the armed state.
- The onboard OLED is hardwired to **GPIO5 (SDA)** and **GPIO6 (SCL)** — these pins are not available for other use.
- **GPIO20/GPIO21** are the board's labelled serial port (UART0), reserved for the USB debug console.
- The camera uses **UART1** on GPIO3 (TX) and GPIO4 (RX), leaving UART0 permanently free for `Serial.print()` debug output.

### 2.3 GPIO Pin Assignment

| Function | GPIO | Direction | Note |
|---|---|---|---|
| OLED SDA | GPIO5 | I2C | Hardwired on board — do not reassign |
| OLED SCL | GPIO6 | I2C | Hardwired on board — do not reassign |
| UART0 RX (USB debug) | GPIO20 | Input | Board-labelled serial port, debug console only |
| UART0 TX (USB debug) | GPIO21 | Output | Board-labelled serial port, debug console only |
| UART1 RX from camera | GPIO4 | Input | Green wire from RunCam TX |
| UART1 TX to camera | GPIO3 | Output | White wire to RunCam RX |
| Arm signal input | GPIO2 | Input (pull-up) | Active low |
| Status LED | GPIO8 | Output | Onboard LED, labelled on board |

All configurable GPIO assignments are defined in `config.h`. GPIO5 and GPIO6 are hardwired to the OLED and must never be reassigned.

---

## 3. Software Architecture

### 3.1 Layer Diagram

```
┌─────────────────────────────────────────────────┐
│                   Web UI (HTML/JS)               │
├─────────────────────────────────────────────────┤
│         REST API + WebSocket Server              │
│              (ESPAsyncWebServer)                 │
├──────────────────────┬──────────────────────────┤
│   FlightController   │     CameraManager         │
│  (arm/record FSM)    │  (config, photo, device)  │
├──────────────────────┴──────────────────────────┤
│              SettingsStore                       │
│    (NVS persistence via Preferences library)     │
├─────────────────────────────────────────────────┤
│              RunCamProtocol                      │
│       (RCSP command encoding / CRC8)             │
├─────────────────────────────────────────────────┤
│              IRunCamTransport (interface)         │
├──────────────┬──────────────────────────────────┤
│  UartTransport│         MockTransport            │
│  (hardware)  │         (unit tests)              │
└──────────────┴──────────────────────────────────┘
         │ state events
         ▼
┌─────────────────────────────────────────────────┐
│              OledDisplay                         │
│  (0.42" SSD1306, 72×40 px, I2C GPIO5/6)         │
└─────────────────────────────────────────────────┘
```

### 3.2 Module Summary

| Module | File(s) | Responsibility |
|---|---|---|
| `IRunCamTransport` | `transport.h` | Abstract interface for send/receive bytes |
| `UartTransport` | `uart_transport.h/.cpp` | Hardware UART1 implementation (GPIO3 TX, GPIO4 RX) |
| `MockTransport` | `mock_transport.h/.cpp` | Test stub, returns canned responses |
| `RunCamProtocol` | `runcam_protocol.h/.cpp` | RCSP frame encoding, CRC8, response parsing |
| `RunCamCamera` | `runcam_camera.h/.cpp` | High-level camera commands (uses protocol + transport) |
| `FlightController` | `flight_controller.h/.cpp` | Arm/record state machine, auto-stop timer |
| `PreflightCheck` | `preflight_check.h/.cpp` | Power-up config validation logic |
| `SettingsStore` | `settings_store.h/.cpp` | NVS persistence via Arduino Preferences library |
| `OledDisplay` | `oled_display.h/.cpp` | 0.42" SSD1306 display driver wrapper, renders system state |
| `WebServer` | `web_server.h/.cpp` | ESPAsyncWebServer setup, route registration |
| `WsNotifier` | `ws_notifier.h/.cpp` | WebSocket push notifications |
| `config.h` | `config.h` | All pin assignments, defaults, timeouts |
| `main.cpp` | `main.cpp` | Arduino `setup()` / `loop()`, wiring of modules |

---

## 4. RunCam Protocol Layer

### 4.1 RCSP Frame Format — **request**

All host → camera commands use this frame:

```
[0xCC] [CMD] [DATA...] [CRC8]
```

- `0xCC` — fixed header byte
- `CMD` — command byte (see section 4.3)
- `DATA` — optional payload bytes (length is implicit per command)
- `CRC8` — CRC-8/DVB-S2 computed over header + cmd + data

**There is no `LEN` byte on requests.** (v1.0 of this doc claimed there was; it was wrong. The implementation always omitted it, so transmitted commands have always been correct — but the documentation has now been corrected to match.)

### 4.1.1 RCSP Frame Format — **response**

Responses come back in **two distinct shapes** depending on the command. The parser must dispatch on the first byte received.

**Shape A — `GET_DEVICE_INFO` only** (5 bytes, header `0xCC`):

```
[0xCC] [proto] [feat_lo] [feat_hi] [CRC8]
```

CRC8 over the first 4 bytes. `proto` = 1 byte protocol version. Features = 2 bytes little-endian.

**Shape B — every other command** (5 or 6 bytes, header `0x55`):

```
ACK:  [0x55] [0x06] [0x01] [0x00] [echoed_action] [CRC8]      ← 6 bytes
NAK:  [0x55] [0x05] [0xFF] [err_code]             [CRC8]      ← 5 bytes
```

`LENGTH` (byte 1) gives the total frame length including header and CRC. `STATUS` (byte 2) is `0x01` for ACK, `0xFF` for NAK. CRC8 is over the entire frame except the CRC byte itself.

**NAK error codes:**

| Code | Meaning |
|------|---------|
| `0x01` | Unknown command or wrong CRC |
| `0x02` | Wrong camera state for this command (e.g. Start while already recording) |
| `0x04` | Invalid argument or unsupported setting ID |

### 4.2 CRC-8 Algorithm

Polynomial: `0xD5` (DVB-S2 standard)
Initial value: `0x00`
Input/output reflected: No

```cpp
uint8_t crc8_dvb_s2(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0xD5 : (crc << 1);
        }
    }
    return crc;
}
```

The CRC is computed over `[header, cmd, data...]` on requests, and over **all preceding bytes including the leading header** on responses (whether the header is `0xCC` or `0x55`). Same algorithm for both.

**Test vector** — `GET_DEVICE_INFO` is `CC 00 60` (the `0x60` is the CRC of `CC 00`; older docs that said `DB` were wrong).

### 4.3 Command Reference

| Command Name | CMD Byte | Direction | Payload |
|---|---|---|---|
| `GET_DEVICE_INFO` | `0x00` | Host→Camera | None |
| `CAMERA_CONTROL` | `0x01` | Host→Camera | 1 byte: action |
| `5KEY_SIMULATION_PRESS` | `0x02` | Host→Camera | 1 byte: key |
| `5KEY_SIMULATION_RELEASE` | `0x03` | Host→Camera | None |
| `5KEY_CONNECTION_EVENT` | `0x04` | Host→Camera | 1 byte: event |
| `GET_SETTINGS` | `0x10` | Host→Camera | 1 byte: setting ID |
| `WRITE_SETTING` | `0x11` | Host→Camera | 1 byte: setting ID + value bytes |
| `READ_SETTING_DETAIL` | `0x12` | Host→Camera | 1 byte: setting ID |

#### Camera Control Action Bytes (`CMD 0x01`)

| Action | Byte |
|---|---|
| Simulate power button | `0x01` |
| Simulate mode button | `0x02` |
| Start recording | `0x03` |
| Stop recording | `0x04` |
| Capture photo | `0x05` |

#### 5-Key Simulation Key Bytes (`CMD 0x02`)

| Key | Byte |
|---|---|
| Up | `0x01` |
| Left | `0x02` |
| Right | `0x03` |
| Down | `0x04` |
| Confirm | `0x05` |

### 4.4 Device Info Response (`CMD 0x00`)

Response is 5 bytes total: `[0xCC] [proto] [feat_lo] [feat_hi] [CRC]`. There is **no** "camera model identifier" byte (v1.0 of this doc was wrong about this — the field doesn't exist on the wire).

The two `feat` bytes form a `uint16_t` little-endian bitmask:

| Bit | Feature | Mask |
|-----|---------|------|
| 0 | `SIMULATE_POWER_BUTTON` | `0x0001` |
| 1 | `SIMULATE_WIFI_BUTTON` | `0x0002` |
| 2 | `CHANGE_MODE` | `0x0004` |
| 3 | `SIMULATE_5_KEY` | `0x0008` (not set on Thumb Pro W) |
| 4 | `DEVICE_SETTINGS_ACCESS` | `0x0010` |
| 5 | `DISPLAY_PORT` | `0x0020` |
| 6 | `START_RECORDING` | `0x0040` |
| 7 | `STOP_RECORDING` | `0x0080` (not set on Thumb Pro W) |
| 8–15 | reserved / future | |

`featureBitmask` must be declared `uint16_t` (it was `uint8_t` in v1.0 — the high byte was being silently truncated).

**Observed Thumb Pro W response:** `CC 01 77 00 02` → proto v1, features `0x0077` (bits 0, 1, 2, 4, 5, 6 set).

The `RunCamCamera` module **must** query device info on startup and cache the protocol version and 16-bit feature bitmask. All subsequent commands check the bitmask before sending, returning an error if the feature is not supported rather than sending an unsupported command — except where empirical evidence shows the feature bit lies (e.g. bit 6 `START_RECORDING` is advertised but action `0x03` returns NAK in practice; see §4.7).

### 4.5 Settings IDs — **DEFERRED — no working ID map for this camera**

The setting-ID map listed in v1.0 of this doc was **invented**, not sourced from RunCam documentation. On-the-wire testing against a Thumb Pro W (see `Serial_Diagnostic_Report.md` §2.4) shows that none of those IDs return valid setting data — they either return NAK 0x04 ("invalid argument") or are silently ignored. The pattern of which IDs respond is inconsistent across runs.

**Implications:**

- `RunCamCamera::writeSetting()` and `readSetting()` are implemented in the protocol layer but are **disabled at the integration layer**.
- The `PreflightCheck` module (§5.2) is also **deferred** — it relies on reading current settings back from the camera, which doesn't work.
- The Settings tab in the web UI must show every control as **"Not supported — RunCam Thumb Pro W setting map not available"** and accept no input.
- The Web UI Status tab no longer shows preflight results.

**Recovery path:** Either RunCam publishes the Thumb Pro W setting map, or someone sniffs the UART traffic of the official RunCam smartphone app to reverse-engineer it. Until then, this section stays deferred.

### 4.6 IRunCamTransport Interface

```cpp
class IRunCamTransport {
public:
    virtual ~IRunCamTransport() = default;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    virtual int receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) = 0;
    virtual void flush() = 0;
};
```

`RunCamProtocol` and `RunCamCamera` depend only on `IRunCamTransport`. `UartTransport` and `MockTransport` both implement this interface.

### 4.7 Response Handling — **[corrected]**

Every command in the §4.3 table receives a response except `5KEY_SIMULATION_*` (silently ignored on the Thumb Pro W) and the camera-initiated `REQUEST_FC_ATTITUDE` (which is the camera asking, not answering).

| Command | Response shape | Notes |
|---|---|---|
| `GET_DEVICE_INFO` (0x00) | `[0xCC]`-format, 5 bytes | parse with `parseDeviceInfo()` |
| `CAMERA_CONTROL` (0x01) | `[0x55]`-format, 5–6 bytes | ACK or NAK — must be read |
| `5KEY_*` (0x02–0x04) | silent | unsupported on this camera |
| `GET_SETTINGS` (0x10) | varies — currently always NAK | see §4.5 |
| `WRITE_SETTING` (0x11) | varies — currently always NAK | see §4.5 |
| `READ_SETTING_DETAIL` (0x12) | varies — currently always NAK | see §4.5 |

**v1.0 of this doc claimed `CAMERA_CONTROL` was fire-and-forget. It is not.** The implementation must read and validate the ACK/NAK reply or it will spend 1.5 s per command timing-out and retrying for no reason.

#### Recording control

Action `0x03` (Start Recording) is advertised by the feature bitmask (`bit 6`) but **returns NAK `0x02` in practice** on the Thumb Pro W firmware tested. Action `0x04` (Stop Recording) is **not** advertised (bit 7 off) yet returns ACK when applicable.

`RunCamCamera::startRecording()` and `stopRecording()` must therefore use action `0x01` (Power button toggle) rather than the explicit `0x03`/`0x04`. The state machine tracks recording state in software and toggles via Power. ACK on the 0x55-format response is treated as confirmation that the command was accepted; the LED / supply current confirms the *physical* state change out-of-band.

#### Timeouts and retries

- Each command waits up to `RUNCAM_RESPONSE_TIMEOUT_MS` (default: 500 ms) for the first response byte.
- Once any byte arrives, the receive continues with an **inter-byte timeout of 200 ms** to handle bursty replies (the camera occasionally retransmits NAK frames 3–4 times back-to-back).
- On timeout or CRC error, the command is retried up to `RUNCAM_MAX_RETRIES` (default: 3) times.
- A `NAK 0x02` is **not** retried — it indicates wrong state, retrying won't change anything; return immediately with a state-error result.
- A `NAK 0x04` is **not** retried — invalid argument; not transient.
- After all retries exhausted on timeout/CRC, `RunCamCamera` returns an error code and logs the failure.
- All UART communication runs at **115200 baud**.

---

## 5. Business Logic & State Machine

### 5.1 Flight Controller State Machine

The `FlightController` module manages the arm/record lifecycle.

```
                    ┌─────────────────────────┐
                    │         IDLE             │
                    │  LED: slow blink (2s)    │
                    └────────────┬─────────────┘
                                 │ ARM_PIN goes LOW
                                 ▼
                    ┌─────────────────────────┐
                    │         ARMED            │
                    │  LED: fast blink (200ms) │
                    │  Send: startRecording()  │
                    └────────────┬─────────────┘
                                 │ startRecording() success
                                 ▼
                    ┌─────────────────────────┐
                    │       RECORDING          │◄──────────────────┐
                    │  LED: solid ON           │                   │
                    │  Timer: 5 min countdown  │                   │ restart=true
                    │  ARM_PIN ignored         │                   │
                    └────────────┬─────────────┘                   │
                         5 min elapsed only                        │
                                 ▼                                 │
                    ┌─────────────────────────┐                   │
                    │       STOPPING           │                   │
                    │  Send: stopRecording()   │                   │
                    │  LED: off (brief)        │                   │
                    └────────────┬─────────────┘                   │
                                 │ stopRecording() success          │
                           ┌─────┴──────┐                         │
                     restart=false     restart=true                │
                           │             └───────────────────────►─┘
                           ▼
                    ┌─────────────────────────┐
                    │         IDLE             │
                    └─────────────────────────┘
```

#### State Definitions

| State | Description |
|---|---|
| `IDLE` | Waiting for arm signal. No recording. |
| `ARMED` | ARM_PIN asserted low. `startRecording()` sent to camera. |
| `RECORDING` | Recording active. 5-minute auto-stop timer running. |
| `STOPPING` | `stopRecording()` sent. Awaiting confirmation. |

#### Transitions

| From | Trigger | To | Action |
|---|---|---|---|
| `IDLE` | `ARM_PIN` LOW | `ARMED` | Send `startRecording()` — internally sends action `0x01` (Power) |
| `ARMED` | `startRecording()` ACK | `RECORDING` | Start 5-min timer |
| `ARMED` | `startRecording()` NAK/timeout | `IDLE` | Log error, notify UI |
| `RECORDING` | Timer elapsed (5 min) | `STOPPING` | Send `stopRecording()` — internally sends action `0x01` (Power) again |
| `STOPPING` | `stopRecording()` ACK, restart=false | `IDLE` | Notify UI |
| `STOPPING` | `stopRecording()` ACK, restart=true | `ARMED` | Send `startRecording()` |

**Note:** `startRecording()` and `stopRecording()` both internally use action `0x01` (Power button), which toggles the camera's recording state. The state machine tracks whether the camera is currently recording so it knows what the Power press will do. Per §4.7, the explicit Start (0x03) and Stop (0x04) actions are unreliable on the Thumb Pro W and are not used.

#### Debounce

`ARM_PIN` is debounced with a 50 ms window on the LOW-going edge (arm trigger) to prevent spurious recording starts from electrical noise. The HIGH-going edge (disarm) is ignored — it has no effect on the recording state machine.

### 5.2 Pre-Flight Check — **DEFERRED**

Settings access (§4.5) is deferred because the camera's setting-ID map is not known. PreflightCheck depends on settings read-back, so it is also deferred.

The `PreflightCheck` module **is built and unit-tested** with mocks for when settings access becomes available. At runtime it produces a synthetic `PreflightResult { passed: true, checks: { /* "settings access not available" */ } }` so the Status tab in the web UI can still render. The status LED never enters the "preflight failed" pattern.

When the setting map becomes available, restore the original behaviour:

1. `SettingsStore` loads all settings from NVS (or writes defaults on first boot).
2. All settings are applied to the camera via `RunCamCamera`.
3. `PreflightCheck` reads back the current values of resolution, FPS, and EIS from the camera.
4. Compares them against the values stored in NVS (i.e. what was just applied).
5. Builds a `PreflightResult` struct with pass/fail per item and overall pass/fail.
6. If any item fails (camera did not accept a setting), the status LED blinks a warning pattern (3 rapid blinks, pause, repeat).
7. The result is stored and exposed via the REST API and WebSocket so the UI displays it on first load.

**Pre-flight does not block operation.** A failure here indicates the camera rejected a setting (e.g. 120fps not available at the current resolution), not a misconfiguration by the user. The result is informational.

### 5.3 Auto-Stop Timer

- Timer starts when the `RECORDING` state is entered.
- Duration: `AUTO_STOP_DURATION_MS` (default: `300000` ms = 5 minutes). Configurable in `config.h`.
- On expiry, `FlightController` transitions to `STOPPING` regardless of arm state.
- The "restart immediately" flag (`autoRestart`) is a boolean held in `FlightController`. It is set by the UI via REST API and persists across recording cycles.

### 5.4 Status LED

| State | Pattern |
|---|---|
| `IDLE`, preflight OK | Slow blink: 2 s on, 2 s off |
| `IDLE`, preflight FAIL | 3 rapid blinks (200 ms), 1 s pause, repeat |
| `ARMED` / `RECORDING` | Solid ON |
| `STOPPING` | Off (brief, < 500 ms) |
| Camera comm error | Alternating rapid blink |

---

## 6. WiFi & Web Server

### 6.1 Access Point Configuration

The ESP32-C3 operates as a **WiFi Access Point (AP)**. It does not connect to an existing network.

| Parameter | Default | Config Key |
|---|---|---|
| SSID | `RunCam-Controller` | `WIFI_SSID` |
| Password | `runcam1234` | `WIFI_PASSWORD` |
| IP address | `192.168.4.1` | Fixed (AP default) |
| Channel | `6` | `WIFI_CHANNEL` |

The web UI is served from `http://192.168.4.1/`.

### 6.2 Web Server Library

**ESPAsyncWebServer** is used for all HTTP and WebSocket handling. It is non-blocking and compatible with the Arduino framework on ESP32-C3.

Required libraries:
- `ESPAsyncWebServer` (me-no-dev/ESPAsyncWebServer)
- `AsyncTCP` (me-no-dev/AsyncTCP)

### 6.3 Static File Serving

The web UI (HTML, CSS, JS) is stored as a single self-contained HTML file embedded in the firmware as a `const char*` PROGMEM string. No SPIFFS or LittleFS is required. The file is served at `GET /`.

The HTML file is generated from a template at build time and inlined. The template source lives in `src/web/index.html`.

---

## 7. Web UI Specification

The web UI is a single-page application served from the ESP32-C3. It communicates with the device via REST API calls and a WebSocket connection.

### 7.1 Layout

The UI is organised into four sections accessible via a tab bar at the bottom of the screen (mobile-first design):

| Tab | Icon | Content |
|---|---|---|
| Status | 🏠 | System status, preflight result, arm state, recording timer |
| Control | 🎮 | Record, photo, button simulation, arm override |
| Settings | ⚙️ | All camera settings (video, image, features) |
| Device | ℹ️ | Device info, firmware version, feature flags |

### 7.2 Status Tab

Displays the following, updated in real time via WebSocket:

- **Preflight result panel:** Green (PASS) or Red (FAIL) banner. On FAIL, lists which checks failed (resolution, FPS, EIS) and their actual vs. expected values.
- **Arm state indicator:** Badge showing current state (`IDLE / ARMED / RECORDING / STOPPING`).
- **Recording timer:** Counts up from 00:00 while in `RECORDING` state. Shows time remaining until auto-stop (e.g. `Auto-stop in 4:23`).
- **Auto-restart checkbox:** Labelled "Restart recording immediately after auto-stop." Checked state is sent to the device via `POST /api/auto-restart` whenever it changes.
- **Camera comms status:** Green dot (OK) or red dot (error), updated from WebSocket.

### 7.3 Control Tab

- **Start Recording** button — calls `POST /api/record/start`. Disabled if already recording.
- **Stop Recording** button — calls `POST /api/record/stop`. Disabled if not recording.
- **Capture Photo** button — calls `POST /api/photo`. Always enabled (camera supports photo mode).
- **Button Simulation** section — 5 buttons: Up, Down, Left, Right, Confirm. Each sends the corresponding 5-key simulation command. Also includes **Power** and **Mode** button simulation.
- **Arm Override** section — A toggle switch to manually set the arm state (for ground testing without a flight computer connected). Sends `POST /api/arm` or `POST /api/disarm`.

### 7.4 Settings Tab — **DEFERRED (see §4.5)**

The settings tab is rendered in the web UI for completeness, but **all controls are disabled** with a banner at the top reading:

> Camera settings are not available for the RunCam Thumb Pro W until the
> per-camera setting-ID map is documented. Recording and basic camera
> control work normally.

The list of controls below is the **intended** future layout once setting access is supported. For now, all REST endpoints under `/api/settings/*` return HTTP 503 Service Unavailable with body `{ "ok": false, "error": "settings access deferred" }`.

#### Video Settings (deferred)

| Setting | UI Control | Options |
|---|---|---|
| Resolution | Select | 4K, 2.7K, 1440p, 1080p, 720p |
| Frame Rate | Select | 120, 60, 30, 24 fps |
| Field of View | Select | Wide, Medium, Narrow |
| Video Format | Select | NTSC, PAL |

#### Image Settings (deferred)

| Setting | UI Control | Options |
|---|---|---|
| Sharpness | Select | High, Medium, Low |
| Exposure | Slider | -2 to +2 (step 1) |
| White Balance | Select | Auto, 2800K–7000K |
| Contrast | Select | High, Medium, Low |
| Saturation | Select | High, Medium, Low |
| Hue | Slider | -180 to +180 (step 1) |

#### Feature Settings (deferred)

| Setting | UI Control |
|---|---|
| EIS (Electronic Image Stabilisation) | Toggle |
| Loop Recording | Toggle |
| Auto-start recording on power-up | Toggle |

### 7.5 Device Tab

Displays read-only information retrieved via `GET /api/device`:

- Protocol version (1 byte from device-info response)
- Supported features list (16-bit feature bitmask decoded to named checkboxes, all read-only)
- **Refresh** button to re-query device info

(The camera does not expose a model identifier or firmware version string over UART — the `GET_DEVICE_INFO` response carries only `proto` + `features`. The Device tab does not display these.)

---

## 8. REST API Specification

All endpoints return JSON. All `POST` endpoints with a body use `Content-Type: application/json`.

### 8.1 System

#### `GET /api/status`

Returns current system status.

**Response:**
```json
{
  "state": "RECORDING",
  "recordingSeconds": 142,
  "autoStopSeconds": 300,
  "autoRestart": false,
  "armPin": false,
  "preflight": {
    "passed": false,
    "checks": {
      "resolution": { "ok": true, "expected": "4K", "actual": "4K" },
      "fps": { "ok": false, "expected": "60", "actual": "30" },
      "eis": { "ok": true, "expected": true, "actual": true }
    }
  },
  "cameraCommsOk": true
}
```

#### `GET /api/device`

Returns device information read from camera. The on-wire `GET_DEVICE_INFO` response carries only protocol version and feature bitmask — there is no model id or firmware version field.

**Response:**
```json
{
  "protocolVersion": 1,
  "featureBitmask": "0x0077",
  "features": {
    "simulatePowerButton": true,
    "simulateWifiButton": true,
    "changeMode": true,
    "simulate5Key": false,
    "deviceSettingsAccess": true,
    "displayPort": true,
    "startRecording": true,
    "stopRecording": false
  }
}
```

### 8.2 Recording Control

#### `POST /api/record/start`

Sends `startRecording()` command to camera. Updates state machine.

**Response (success):**
```json
{ "ok": true }
```

**Response (failure):**
```json
{ "ok": false, "error": "Camera did not acknowledge command" }
```

#### `POST /api/record/stop`

Sends `stopRecording()` command.

**Response:** Same as above.

#### `POST /api/auto-restart`

Sets the auto-restart flag.

**Body:**
```json
{ "enabled": true }
```

**Response:**
```json
{ "ok": true, "autoRestart": true }
```

### 8.3 Camera Control

#### `POST /api/photo`

Commands the camera to capture a still photo.

**Response:**
```json
{ "ok": true }
```

#### `POST /api/button`

Simulates a camera button press.

**Body:**
```json
{ "button": "mode" }
```

Accepted values for `button`: `"power"`, `"mode"`, `"up"`, `"down"`, `"left"`, `"right"`, `"confirm"`

**Response:**
```json
{ "ok": true }
```

### 8.4 Arm Control (Manual Override)

#### `POST /api/arm`

Manually asserts the armed state (ground testing only). Does not affect hardware GPIO.

**Response:**
```json
{ "ok": true, "state": "ARMED" }
```

#### `POST /api/disarm`

Clears manual arm override.

**Response:**
```json
{ "ok": true, "state": "IDLE" }
```

### 8.5 Settings — **DEFERRED (see §4.5)**

All endpoints under `/api/settings/*` return HTTP **503 Service Unavailable** with body `{ "ok": false, "error": "settings access deferred" }` until the camera's setting-ID map is documented. The endpoint shapes below are kept here for future reference.

#### `GET /api/settings`

Returns all current camera settings.

**Response:**
```json
{
  "video": {
    "resolution": "4K",
    "fps": 60,
    "fov": "Wide",
    "format": "PAL"
  },
  "image": {
    "sharpness": "Medium",
    "exposure": 0,
    "whiteBalance": "Auto",
    "contrast": "Medium",
    "saturation": "Medium",
    "hue": 0
  },
  "features": {
    "eis": true,
    "loopRecording": false,
    "autoStartRecording": false
  }
}
```

#### `POST /api/settings/{settingId}`

Writes a single setting.

**URL parameter:** `settingId` — one of: `resolution`, `fps`, `fov`, `format`, `sharpness`, `exposure`, `whiteBalance`, `contrast`, `saturation`, `hue`, `eis`, `loopRecording`, `autoStartRecording`

**Body:**
```json
{ "value": "4K" }
```

**Response (success):**
```json
{ "ok": true, "settingId": "resolution", "value": "4K" }
```

**Response (unsupported):**
```json
{ "ok": false, "error": "Setting not supported by this camera" }
```

**Response (invalid value):**
```json
{ "ok": false, "error": "Invalid value '8K' for setting 'resolution'" }
```

---

## 9. WebSocket Protocol

A single WebSocket endpoint is available at `ws://192.168.4.1/ws`.

The device pushes status updates to all connected clients whenever state changes. The client does not send messages over WebSocket (control is via REST only).

### 9.1 Status Update Message

Sent whenever any of the following change: flight state, recording timer (every 1 second while recording), camera comms status, arm pin state.

```json
{
  "type": "status",
  "state": "RECORDING",
  "recordingSeconds": 143,
  "autoStopSeconds": 300,
  "autoRestart": false,
  "armPin": false,
  "cameraCommsOk": true
}
```

### 9.2 Preflight Update Message

Sent once on startup after preflight check completes.

```json
{
  "type": "preflight",
  "passed": true,
  "checks": {
    "resolution": { "ok": true, "expected": "4K", "actual": "4K" },
    "fps": { "ok": true, "expected": "60", "actual": "60" },
    "eis": { "ok": true, "expected": true, "actual": true }
  }
}
```

### 9.3 Error Message

Sent on camera communication errors.

```json
{
  "type": "error",
  "code": "UART_TIMEOUT",
  "message": "Camera did not respond within 500ms"
}
```

---

## 10. Configuration

All configurable parameters are defined in `src/config.h`. The coder LLM must not hardcode any of these values in logic modules.

```cpp
// WiFi
#define WIFI_SSID              "RunCam-Controller"
#define WIFI_PASSWORD          "runcam1234"
#define WIFI_CHANNEL           6

// GPIO — Camera UART (UART1, no conflict with USB debug console)
#define GPIO_UART_TX           3    // to RunCam RX (white wire)
#define GPIO_UART_RX           4    // to RunCam TX (green wire)
#define CAMERA_UART_NUM        1    // UART1

// GPIO — OLED (hardwired on this board, do not change)
#define GPIO_OLED_SDA          5
#define GPIO_OLED_SCL          6
#define OLED_I2C_ADDR          0x3C
#define OLED_WIDTH             72
#define OLED_HEIGHT            40

// GPIO — Control
#define GPIO_ARM_PIN           2    // Active low, internal pull-up
#define GPIO_STATUS_LED        8    // Onboard LED

// Serial
#define RUNCAM_BAUD_RATE       115200
#define RUNCAM_RESPONSE_TIMEOUT_MS  500
#define RUNCAM_MAX_RETRIES     3

// Flight logic
#define ARM_DEBOUNCE_MS        50
#define AUTO_STOP_DURATION_MS  300000   // 5 minutes

// Default camera settings (written to NVS on first boot, user-adjustable thereafter)
// 1080p is the highest resolution supported at 120fps on the RunCam Thumb Pro 4K v2
#define DEFAULT_RESOLUTION      3   // 1080p
#define DEFAULT_FPS             0   // 120fps
#define DEFAULT_FOV             0   // Wide
#define DEFAULT_VIDEO_FORMAT    1   // PAL
#define DEFAULT_EIS             1   // enabled
#define DEFAULT_LOOP_RECORDING  0   // disabled
#define DEFAULT_AUTO_START_REC  0   // disabled
#define DEFAULT_SHARPNESS       1   // Medium
#define DEFAULT_EXPOSURE        0   // 0 EV
#define DEFAULT_WHITE_BALANCE   0   // Auto
#define DEFAULT_CONTRAST        1   // Medium
#define DEFAULT_SATURATION      1   // Medium
#define DEFAULT_HUE             0   // 0

// NVS namespace
#define NVS_NAMESPACE           "runcam"

// WebSocket
#define WS_STATUS_INTERVAL_MS  1000

// OLED display refresh
#define OLED_REFRESH_INTERVAL_MS  500
```

## 11. OLED Display

### 11.1 Hardware

The board has a **0.42-inch SSD1306 OLED** with a usable resolution of **72×40 pixels**, connected via I2C on GPIO5 (SDA) and GPIO6 (SCL) at fixed I2C address `0x3C`. This is a monochrome display, white pixels on black background.

At 72×40 px, two lines of text fit comfortably using the 6×8 px built-in font at scale 1 (up to 12 characters per line).

### 11.2 Library

Use **Adafruit SSD1306** with **Adafruit GFX**:

```ini
lib_deps =
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit GFX Library @ ^1.11.9
```

Initialise with the non-standard resolution:
```cpp
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
// Wire.begin(GPIO_OLED_SDA, GPIO_OLED_SCL) called in setup()
```

### 11.3 OledDisplay Module

`OledDisplay` is a passive rendering module with a single `render(const SystemState& state)` method, called from `loop()` every `OLED_REFRESH_INTERVAL_MS` (500 ms). It contains no business logic — it only translates state to pixels.

The `SystemState` struct is shared with `WsNotifier` and the REST API handlers:

```cpp
struct SystemState {
    FlightState flightState;          // IDLE, ARMED, RECORDING, STOPPING
    uint32_t    recordingSeconds;
    uint32_t    autoStopSeconds;
    bool        preflightPassed;
    bool        preflightDone;
    bool        cameraCommsOk;
    char        preflightFailItems[16]; // e.g. "RES FPS EIS"
};
```

### 11.4 Screen Layout

Two rows, 6×8 px font at scale 1 (`setTextSize(1)`), white on black (`SSD1306_WHITE`).

| State | Row 1 (top) | Row 2 (bottom) |
|---|---|---|
| Boot / init | `RunCam Ctrl` | `Init...` |
| Preflight running | `Preflight` | `Checking...` |
| Preflight PASS | `Preflight OK` | `Ready` |
| Preflight FAIL | `PREFLT FAIL` | Failed items e.g. `FPS EIS` |
| IDLE, comms OK | `IDLE` | `192.168.4.1` |
| IDLE, comms error | `CAM ERROR` | `Check UART` |
| ARMED | `ARMED` | `Starting...` |
| RECORDING | `REC 04:23` | `Stop 0:37` |
| STOPPING | `Stopping` | _(blank)_ |

**Recording:** Row 1 counts up elapsed time (`REC MM:SS`); Row 2 counts down to auto-stop (`Stop M:SS`).

**IDLE Row 2** shows `192.168.4.1` so the user can confirm the WiFi AP is up and knows where to connect without a serial monitor.

**Preflight FAIL Row 2** shows abbreviated failed check names space-separated — `RES`, `FPS`, `EIS` — max 11 chars, fits within 72 px at scale 1.

### 11.5 OledDisplay Testing

`OledDisplay` uses hardware I2C and is excluded from host unit tests (`#ifndef UNIT_TEST`). The `SystemState` struct it consumes is fully covered by flight controller and preflight check unit tests.

---

## 12. Settings Store

### 12.1 Overview

`SettingsStore` provides persistent storage for all camera settings using the Arduino **Preferences** library, which wraps the ESP32 NVS (Non-Volatile Storage) flash partition. Settings survive power cycles and are applied to the camera on every boot.

### 12.2 NVS Layout

All keys are stored under the namespace `"runcam"`. Keys are short strings (NVS limit: 15 chars).

| NVS Key | Setting | Default |
|---|---|---|
| `res` | Video resolution | `3` (1080p) |
| `fps` | Frame rate | `0` (120fps) |
| `fov` | Field of view | `0` (Wide) |
| `fmt` | Video format | `1` (PAL) |
| `eis` | EIS | `1` (on) |
| `loop` | Loop recording | `0` (off) |
| `autostart` | Auto-start recording | `0` (off) |
| `sharp` | Sharpness | `1` (Medium) |
| `exposure` | Exposure | `0` |
| `wb` | White balance | `0` (Auto) |
| `contrast` | Contrast | `1` (Medium) |
| `saturation` | Saturation | `1` (Medium) |
| `hue` | Hue | `0` |
| `autostop` | Auto-stop duration (ms) | `300000` |
| `restart` | Auto-restart after stop | `0` (off) |

### 12.3 First Boot Detection

NVS returns a default value when a key does not exist. `SettingsStore` uses a dedicated key `"init"` (uint8, value `1`) to detect whether NVS has been initialised. If absent, defaults are written for all keys before any other operation.

```cpp
class SettingsStore {
public:
    void begin();                          // init NVS, write defaults if first boot
    CameraSettings load();                 // read all settings from NVS
    bool save(const CameraSettings& s);    // write all settings to NVS
    bool saveSetting(const char* key, int32_t value); // write single setting
    void resetToDefaults();                // overwrite NVS with compiled-in defaults
};
```

### 12.4 Boot Sequence

```
setup()
  └─► SettingsStore::begin()          // init NVS, write defaults if first boot
  └─► RunCamCamera::begin()           // init UART1, query device info + feature flags
  └─► settings = SettingsStore::load()
  └─► RunCamCamera::applySettings(settings)  // write all settings to camera
  └─► PreflightCheck::run()           // read back from camera, compare to NVS values
  └─► WiFi AP + WebServer start
  └─► OledDisplay: show preflight result
```

### 12.5 Settings Change Flow (Web UI)

When the user changes a setting via the web UI:

1. `POST /api/settings/{id}` received by WebServer
2. Value validated against allowed range/enum
3. `RunCamCamera::writeSetting(id, value)` called
4. On success: `SettingsStore::saveSetting(key, value)` called — NVS updated
5. On failure: NVS not updated, error returned to UI
6. WebSocket status push sent to all clients

NVS is only written after confirmed camera acceptance. A camera rejection does not corrupt stored settings.

### 12.6 Reset to Defaults

`POST /api/settings/reset` endpoint calls `SettingsStore::resetToDefaults()` followed by `RunCamCamera::applySettings()`. The UI shows a confirmation dialog before sending this request.

### 12.7 SettingsStore Testing

`SettingsStore` wraps the `Preferences` library which is ESP32-specific. For host unit tests, a `MockPreferences` stub is provided that implements the same interface using a `std::map<std::string, int32_t>` in memory.

Test cases for `test_settings_store.cpp`:

| Test | Action | Expected |
|---|---|---|
| First boot | `begin()` on empty store | Defaults written, `init` key set |
| Load defaults | `load()` after first boot | Resolution=3, FPS=0, EIS=1, etc. |
| Save and reload | `save()` then `load()` | All values round-trip correctly |
| Single setting save | `saveSetting("fps", 1)` | Only FPS updated, others unchanged |
| Reset to defaults | `resetToDefaults()` then `load()` | All values match compiled-in defaults |
| Out-of-range value | `saveSetting("fps", 99)` | Validation error, NVS not written |

---

## 13. Testing Strategy

### 11.1 Overview

All business logic, protocol encoding, and state machine code must be testable on a **Linux host** without any ESP32 hardware. Tests run using a standard C++ test runner (Unity or Google Test). The coder LLM must implement tests alongside each module and must be able to run `make test` to execute the full test suite.

### 11.2 Test Structure

```
tests/
├── test_crc8.cpp
├── test_protocol_encoding.cpp
├── test_protocol_parsing.cpp
├── test_flight_controller.cpp
├── test_preflight_check.cpp
├── test_settings_validation.cpp
├── test_api_handlers.cpp
└── mocks/
    ├── mock_transport.h/.cpp
    └── mock_camera.h/.cpp
```

### 11.3 MockTransport

`MockTransport` implements `IRunCamTransport`. It maintains:

- A queue of byte sequences to return on `receive()` calls (`setResponseQueue()`).
- A log of all bytes passed to `send()` for assertion.

```cpp
class MockTransport : public IRunCamTransport {
public:
    void enqueueResponse(const std::vector<uint8_t>& response);
    std::vector<uint8_t> getSentBytes() const;
    void reset();

    bool send(const uint8_t* data, size_t len) override;
    int receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) override;
    void flush() override;
};
```

### 11.4 Test Cases — Protocol Layer — **[corrected to match wire-verified protocol]**

#### `test_crc8.cpp`

| Test | Input | Expected CRC |
|---|---|---|
| GET_DEVICE_INFO request | `[0xCC, 0x00]` | `0x60` (verified on hardware) |
| WiFi button | `[0xCC, 0x01, 0x00]` | `0x32` |
| Power button | `[0xCC, 0x01, 0x01]` | `0xE7` |
| Mode | `[0xCC, 0x01, 0x02]` | `0x4D` |
| Start recording | `[0xCC, 0x01, 0x03]` | `0x98` |
| Stop recording | `[0xCC, 0x01, 0x04]` | `0xCC` |
| Device info reply (no CRC) | `[0xCC, 0x01, 0x77, 0x00]` | `0x02` |
| ACK frame (no CRC) | `[0x55, 0x06, 0x01, 0x00, 0x02]` | `0x63` |
| NAK frame (no CRC) | `[0x55, 0x05, 0xFF, 0x02]` | `0x1A` |

All vectors verified against a Python reference and against the physical camera.

#### `test_protocol_encoding.cpp`

For each command, verify that `buildFrame()` produces the correct byte sequence. Request format is `[0xCC, CMD, data..., CRC]` (no LEN byte):

- `buildGetDeviceInfoFrame()` → `[0xCC, 0x00, 0x60]`
- `buildPowerButtonFrame()` → `[0xCC, 0x01, 0x01, 0xE7]` (record toggle, see §4.7)
- `buildModeFrame()` → `[0xCC, 0x01, 0x02, 0x4D]`
- `buildCapturePhotoFrame()` → `[0xCC, 0x01, 0x05, ??]` (compute CRC at build)
- `buildKeyPressFrame(KEY_CONFIRM)` → `[0xCC, 0x02, 0x05, ??]` (will be NAK'd by Thumb Pro W but encoding must be correct)
- `buildGetSettingFrame(0x01)` → `[0xCC, 0x10, 0x01, ??]`
- `buildWriteSettingFrame(0x01, 0x00)` → `[0xCC, 0x11, 0x01, 0x00, ??]`

#### `test_protocol_parsing.cpp`

Tests must cover **both** response shapes (§4.1.1).

- Valid `0xCC`-format device info response `CC 01 77 00 02` → `DeviceInfo { proto=1, features=0x0077 }`
- Valid `0x55`-format ACK `55 06 01 00 02 63` → `Response { kind=ACK, actionEcho=0x02 }`
- Valid `0x55`-format NAK `55 05 FF 02 1A` → `Response { kind=NAK, errCode=0x02 }`
- NAK 0x01 (`55 05 FF 01 B0`) → errCode 0x01
- NAK 0x04 (`55 05 FF 04 9B`) → errCode 0x04
- Wrong CRC on `0xCC` response → `PARSE_ERROR_CRC`
- Wrong CRC on `0x55` response → `PARSE_ERROR_CRC`
- Unknown first byte (neither `0xCC` nor `0x55`) → `PARSE_ERROR_HEADER`
- Empty response → `PARSE_ERROR_TIMEOUT`
- Truncated `0x55` frame (length byte says 6, only 4 bytes received) → `PARSE_ERROR_INCOMPLETE`
- Multi-frame buffer with two queued NAKs → parser advances past first frame, returns second when re-called

### 11.5 Test Cases — Flight Controller

#### `test_flight_controller.cpp`

The `FlightController` is constructed with a `MockCamera` (which implements a `ICamera` interface with `startRecording()`, `stopRecording()` methods returning configurable results).

| Test | Setup | Event | Expected State | Expected Actions |
|---|---|---|---|---|
| Idle → Armed | Camera comms OK | `ARM_PIN` LOW | `ARMED` | `startRecording()` called |
| Armed → Recording | `startRecording()` returns OK | — | `RECORDING` | Timer started |
| Armed → Idle on failure | `startRecording()` returns ERROR | — | `IDLE` | Error logged |
| Recording → Stopping on timer | In RECORDING state | 300001 ms elapsed | `STOPPING` | `stopRecording()` called |
| Auto-stop, restart=false | STOPPING, restart=false | `stopRecording()` OK | `IDLE` | — |
| Auto-stop, restart=true | STOPPING, restart=true | `stopRecording()` OK | `ARMED` | `startRecording()` called |
| Disarm ignored in RECORDING | In RECORDING | ARM_PIN HIGH (any duration) | `RECORDING` | No transition, no action |

### 11.6 Test Cases — Preflight Check — **DEFERRED MODE**

PreflightCheck (§5.2) returns a synthetic "settings access not available" result rather than reading settings from the camera. Tests verify this deferred behaviour and keep the original test cases as `#ifdef`'d skips for the future restoration.

#### `test_preflight_check.cpp`

| Test | Setup | Expected Result |
|---|---|---|
| Deferred default | None | `passed=true`, check `"settings_access"` = `not_available` |
| Original tests | wrapped in `#ifdef PREFLIGHT_ENABLED` | currently disabled |

### 11.7 Test Cases — Settings Validation — **DEFERRED**

Settings validation logic is built (and kept tested) so it can be reused once the real setting-ID map is available. The tests below operate on the validator in isolation, with no camera dependency, so they continue to pass.

#### `test_settings_validation.cpp`

| Test | Input | Expected |
|---|---|---|
| Valid resolution "4K" | `"4K"` | Byte `0x00` |
| Valid FPS 60 | `60` | Byte `0x01` |
| Invalid resolution | `"8K"` | `VALIDATION_ERROR` |
| Invalid FPS | `90` | `VALIDATION_ERROR` |
| Exposure in range | `-2` | OK |
| Exposure out of range | `-3` | `VALIDATION_ERROR` |
| Hue in range | `180` | OK |
| Hue out of range | `181` | `VALIDATION_ERROR` |

### 11.8 Test Cases — API Handlers

#### `test_api_handlers.cpp`

These tests exercise the request handler functions directly (not over HTTP) by calling them with a mock request object and checking the JSON response.

| Test | Endpoint | Expected Response |
|---|---|---|
| Status in IDLE | `GET /api/status` | `state="IDLE"`, `recordingSeconds=0` |
| Status in RECORDING | `GET /api/status` | `state="RECORDING"`, `recordingSeconds>0` |
| Start recording OK | `POST /api/record/start` | `ok=true` |
| Start recording fails | `POST /api/record/start` (camera error) | `ok=false`, error message present |
| Invalid button name | `POST /api/button {"button":"invalid"}` | HTTP 400, `ok=false` |
| Valid setting write | `POST /api/settings/resolution {"value":"4K"}` | `ok=true` |
| Unsupported setting | `POST /api/settings/resolution` (feature not in bitmask) | `ok=false` |
| Invalid setting value | `POST /api/settings/fps {"value":"90"}` | HTTP 400, `ok=false` |

### 11.9 Build and Test Instructions for Coder LLM

```
project/
├── src/                  # Firmware source (Arduino)
├── tests/                # Host-native unit tests
├── Makefile              # `make test` runs all tests on Linux host
└── platformio.ini        # PlatformIO project configuration
```

**To build and upload firmware:**
```bash
pio run --target upload
```

**To run all tests on Linux host:**
```bash
make test
```

**To run a single test file:**
```bash
make test TEST=tests/test_crc8.cpp
```

The `Makefile` compiles test files against the `src/` modules with `-DUNIT_TEST` defined. All ESP32-specific headers (`Arduino.h`, `HardwareSerial.h`, etc.) are stubbed in `tests/stubs/`. When `UNIT_TEST` is defined, `UartTransport` is excluded from compilation and `MockTransport` is used instead.

### 11.10 Implement-Test-Fix Cycle Instructions

The coder LLM should follow this workflow for each module:

1. Implement the module interface (`.h` file) as specified.
2. Write all test cases for that module before writing the implementation.
3. Run `make test` — expect failures.
4. Implement the module (`.cpp` file).
5. Run `make test` — fix any failures.
6. Repeat until all tests pass.
7. Move to the next module.

**Module implementation order (dependency order):**

1. `crc8` (no dependencies)
2. `RunCamProtocol` (depends on crc8)
3. `MockTransport` (depends on IRunCamTransport)
4. `RunCamCamera` (depends on RunCamProtocol, IRunCamTransport)
5. `SettingsStore` (depends on Preferences library only — no camera dependency)
6. `PreflightCheck` (depends on RunCamCamera, SettingsStore)
7. `FlightController` (depends on RunCamCamera)
8. `OledDisplay` (depends on SystemState; hardware only, verify on device)
9. `WebServer` + `WsNotifier` (depends on FlightController, RunCamCamera, SettingsStore)
10. `main.cpp` (wires everything together with real hardware)

---

## 14. File & Directory Structure

```
runcam-esp32c3/
├── platformio.ini
├── Makefile
├── README.md
├── src/
│   ├── main.cpp
│   ├── config.h
│   ├── system_state.h          ← shared SystemState struct
│   ├── transport/
│   │   ├── i_transport.h
│   │   └── uart_transport.h/.cpp
│   ├── protocol/
│   │   ├── crc8.h/.cpp
│   │   └── runcam_protocol.h/.cpp
│   ├── camera/
│   │   ├── runcam_camera.h/.cpp
│   │   └── camera_settings.h
│   ├── flight/
│   │   ├── flight_controller.h/.cpp
│   │   └── preflight_check.h/.cpp
│   ├── storage/
│   │   └── settings_store.h/.cpp
│   ├── display/
│   │   └── oled_display.h/.cpp ← excluded from UNIT_TEST builds
│   └── web/
│       ├── web_server.h/.cpp
│       ├── ws_notifier.h/.cpp
│       └── index.html          ← compiled into firmware as PROGMEM string
├── tests/
│   ├── test_crc8.cpp
│   ├── test_protocol_encoding.cpp
│   ├── test_protocol_parsing.cpp
│   ├── test_flight_controller.cpp
│   ├── test_preflight_check.cpp
│   ├── test_settings_validation.cpp
│   ├── test_api_handlers.cpp
│   ├── mocks/
│   │   ├── mock_transport.h/.cpp
│   │   └── mock_camera.h/.cpp
│   └── stubs/
│       ├── Arduino.h           ← stub for host builds
│       ├── HardwareSerial.h    ← stub for host builds
│       └── WiFi.h              ← stub for host builds
└── scripts/
    └── html_to_progmem.py      ← converts index.html to C header
```

---

## 15. Dependencies

### 13.1 Firmware (PlatformIO)

```ini
[env:esp32-c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
lib_deps =
    me-no-dev/ESPAsyncWebServer @ ^1.2.3
    me-no-dev/AsyncTCP @ ^1.1.1
    bblanchon/ArduinoJson @ ^7.0.0
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit GFX Library @ ^1.11.9
monitor_speed = 115200
```

### 13.2 Test Runner (Host)

- **Unity** test framework (single header, C, widely used in embedded projects)
- Standard `g++` / `clang++` with C++17
- `ArduinoJson` (fetched as a standalone header for host builds)

### 13.3 Build Tools

- PlatformIO Core (CLI)
- Python 3 (for `html_to_progmem.py` script)
- GNU Make

---

## 16. Implementation Notes for Coder LLM

### 14.1 Coding Style

- C++17 standard.
- Use `enum class` for all state and setting enumerations.
- No dynamic memory allocation in the protocol or state machine layer. Use fixed-size buffers.
- Use `uint8_t`, `uint16_t`, `uint32_t` for all protocol-level types.
- All public functions return a result type (e.g. `RunCamResult` enum) rather than throwing exceptions.
- Log all UART sends and receives at `DEBUG` level using a simple `LOG_DEBUG(msg)` macro that maps to `Serial.println()` on hardware and `printf()` in tests.

### 14.2 Key Implementation Constraints

- `FlightController::update()` must be called from `loop()` on every iteration. It must never block.
- WebSocket status pushes must not be sent from within an interrupt context.
- The `ARM_PIN` must be read with debounce logic inside `FlightController::update()` using a timestamp comparison, not `delay()`.
- All JSON serialisation uses **ArduinoJson v7**.
- The embedded HTML must be generated by `scripts/html_to_progmem.py` into `src/web/web_ui.h` as a `const char WEB_UI[] PROGMEM = ...` declaration. This script must be run as part of the build process (PlatformIO `extra_scripts`).

### 14.3 Potential Pitfalls

- **ESPAsyncWebServer callbacks run on a different task** from `loop()`. Shared state (e.g. `FlightController` state, `autoRestart` flag) must be protected with a mutex or accessed atomically.
- **RunCam protocol responses** may include unsolicited bytes before the response frame. The parser must scan for the `0xCC` header byte rather than assuming it is always the first byte received.
- **CRC8 implementation** — use the DVB-S2 polynomial (`0xD5`), not CRC-8/MAXIM or other variants. Validate against known test vectors before proceeding with higher-level tests.
- **MockTransport `receive()`** should simulate the timeout behaviour: if the response queue is empty and `timeoutMs` expires, return 0 bytes (not an error code).
- **ArduinoJson on host** — include the single-header version; do not rely on PlatformIO library resolution in the test build.

### 14.4 Definition of Done

A module is considered complete when:

1. All unit tests for that module pass with `make test`.
2. The module compiles without warnings under both the Arduino (PlatformIO) and host (g++) build targets.
3. No hardcoded magic numbers — all constants reference `config.h` or local named constants.
4. All public functions are documented with a one-line comment describing inputs, outputs, and side effects.
