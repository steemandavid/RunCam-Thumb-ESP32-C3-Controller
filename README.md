# RunCam Thumb ESP32-C3 Controller

ESP32-C3 based controller for a RunCam Thumb Pro W action camera. Powers on
via a screw switch, auto-starts recording on boot, exposes a WiFi web UI for
manual control, and shows recording status on a 0.42" OLED.

## Hardware

| Part | Details |
|---|---|
| MCU | ESP32-C3 OLED dev board (built-in 128x32 SSD1306, I2C on GPIO5/6) |
| Camera | RunCam Thumb Pro W |
| Power | 1S LiPo (3.5–4.2 V) via on/off screw switch |
| Camera UART | UART1, GPIO3 TX → cam RX (white), GPIO4 RX ← cam TX (green) at 115200 8N1 |
| Status LED | GPIO8 (onboard) |

Power is controlled by a physical screw switch that supplies both the ESP32
and camera. The camera starts recording automatically on boot when the
auto-start setting is enabled (default).

## Build / Flash

```bash
# Host unit tests
make test                # 89/89 tests pass

# Firmware
pio run                  # build
pio run --target upload  # flash via USB JTAG/serial
pio device monitor       # 115200 baud serial monitor
```

The web UI is served from `http://192.168.4.1/` after connecting to the
`RunCam-Controller` WiFi access point (password `runcam1234`).

## Project Layout

```
src/
  protocol/          RCSP framing, CRC8/DVB-S2, response parsing
  camera/            RunCamCamera facade (Power-toggle recording, button sim)
  flight/            FlightController FSM (IDLE / RECORDING)
  storage/           NVS-backed settings store with IPreferences abstraction
  transport/         UART1 transport with IRunCamTransport interface
  display/           SSD1306 OLED render (128x32)
  web/               ESPAsyncWebServer REST + WebSocket + single-page UI
  main.cpp           Boot sequence + main loop
tests/               Host-side Unity tests (89 total)
  mocks/             MockCamera, MockTransport, MockPreferences
  stubs/             Arduino.h, HardwareSerial.h, WiFi.h for host compile
  unity/             Unity test framework
scripts/             HTML → PROGMEM build pipeline
```

## Architecture

### Layer Diagram

```
┌─────────────┐  ┌──────────────┐  ┌──────────────┐
│   main.cpp   │  │  WebServer   │  │  WsNotifier  │
│  (boot+loop) │  │  (REST API)  │  │  (WebSocket) │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │                 │                  │
       └────────┬────────┴──────────────────┘
                │  (all under camMutex)
       ┌────────▼────────┐
       │ FlightController│   OledDisplay ← SystemState
       │  (IDLE/RECORDING)│
       └────────┬─────────┘
                │
       ┌────────▼────────┐
       │  RunCamCamera   │   SettingsStore ← NVS
       │  (ICamera)      │
       └────────┬────────┘
                │
       ┌────────▼────────┐
       │  RunCamProtocol │   CRC8/DVB-S2
       │  (buildFrame +  │
       │   parseResponse)│
       └────────┬────────┘
                │
       ┌────────▼────────┐
       │  UartTransport  │   UART1 115200 8N1
       │  (IRunCamTransport)│
       └─────────────────┘
```

### Thread Safety

A FreeRTOS mutex (`camMutex`) guards all access to the camera and flight
controller. Both the main loop and web server callbacks acquire this mutex
before calling into the shared components. The `CamLock` RAII wrapper in
`web_server.cpp` ensures the mutex is always released.

### Boot Sequence

1. USB CDC serial + status LED GPIO init
2. NVS settings load via `SettingsStore::begin()`
3. UART1 init at 115200 baud (GPIO3 TX, GPIO4 RX)
4. `RunCamCamera::begin()` — sends `GET_DEVICE_INFO`, caches protocol version
   and feature bitmask
5. OLED init — SSD1306 I2C at 0x3C, full GDDRAM clear
6. Auto-start recording if `autoStartRec` setting is enabled and camera OK
7. WiFi AP start (`RunCam-Controller`, password `runcam1234`, channel 6)
8. Main loop: update FlightController state, render OLED (500 ms), push
   WebSocket status (1000 ms)

### Main Loop

```
loop() @ 5 ms tick:
  1. camMutex lock → flight.update(now) → read state → camMutex unlock
  2. updateStatusLed()  — slow blink (IDLE) or solid (RECORDING)
  3. OLED render       — every 500 ms
  4. WebSocket push    — every 1000 ms
  5. webServer.update() — WS client cleanup
```

## Module Reference

### protocol/ — RCSP Framing

Implements the RunCam Serial Communication Protocol (RCSP).

**`runcam_protocol.h`** — Constants and frame structure:
- Request frame: `[0xCC] [cmd] [data...] [CRC8]`
- Device info response: `[0xCC] [protoVer] [featuresLo] [featuresHi] [CRC8]`
- ACK/NAK response: `[0x55] [totalLen] [status] [actionEcho?] [errCode?] [CRC8]`
- Commands: `GET_DEVICE_INFO` (0x00), `CAMERA_CONTROL` (0x01), `5KEY_PRESS`
  (0x02), `5KEY_RELEASE` (0x03), `5KEY_EVENT` (0x04), `GET_SETTINGS` (0x10),
  `WRITE_SETTING` (0x11), `READ_SETTING_DETAIL` (0x12)

**`runcam_protocol.cpp`**:
- `buildFrame()` — constructs a complete RCSP request frame with CRC8 trailer
- `parseResponse()` — parses camera response; dispatches on header byte (0xCC
  for device info, 0x55 for ACK/NAK). Returns `ParseResult` enum and populates
  `ParsedResponse` union-like struct with `frameLength` for caller advancement.
- `parseDeviceInfo()` — convenience extractor from DeviceInfo responses

**`crc8.cpp`** — CRC8/DVB-S2 implementation (polynomial 0xD5). Used for frame
integrity on both request and response sides.

### camera/ — RunCamCamera Facade

**`runcam_camera.h/.cpp`** — High-level camera control implementing the `ICamera`
interface:
- `begin()` — sends `GET_DEVICE_INFO`, caches protocol version and feature
  bitmask. Must be called before any other method.
- `startRecording()` / `stopRecording()` — toggle recording via Power button
  action (0x01). The Thumb Pro W firmware does not reliably support explicit
  Start (0x03) / Stop (0x04), so both start and stop send the same toggle
  command. Recording state is tracked host-side.
- `capturePhoto()` — sends PHOTO action (0x05)
- `simulatePowerButton()` / `simulateModeButton()` / `simulateWifiButton()` —
  individual button simulation via `CAMERA_CONTROL`
- `simulateKeyPress()` / `keyRelease()` / `connectionEvent()` — 5-key OSD
  navigation
- `writeSetting()` / `readSetting()` — deferred; camera setting byte IDs for
  the Thumb Pro W are unknown
- `sendRequest()` — retry loop (up to `RUNCAM_MAX_RETRIES` = 3). Retries on
  timeout/CRC/transport errors. Does NOT retry on NAK (camera rejected the
  command — returns immediately with a `REJECTED_*` code).

**`camera_result.h`** — Result enum covering OK, transport errors (TIMEOUT,
CRC, HEADER, INCOMPLETE, TRANSPORT), state errors (NOT_INITIALISED,
NOT_SUPPORTED), and NAK rejections (REJECTED_STATE, REJECTED_ARG,
REJECTED_UNKNOWN).

**`camera_settings.h/.cpp`** — `SettingId` enum (resolution, FPS, FOV, etc.),
`CameraSettings` struct, validation ranges, NVS key mapping.

### flight/ — FlightController FSM

**`flight_controller.h/.cpp`** — Two-state FSM:
- **IDLE** → `forceStartRecording()` → **RECORDING**
- **RECORDING** → `forceStopRecording()` → **IDLE**
- `update()` is a no-op (no external triggers — all state changes are explicit)
- `getRecordingSeconds()` computes elapsed time from `recordingStartMs_`
- NAK results from camera are surfaced to the caller (no automatic retries)
- Start while already recording, or stop while idle, are idempotent no-ops

**`i_camera.h`** — Minimal interface (`startRecording`, `stopRecording`,
`readSetting`) enabling dependency injection for tests.

### storage/ — NVS Settings

**`settings_store.h/.cpp`** — Key-value settings persisted in ESP32 NVS:
- `IPreferences` abstract interface wraps ESP32 `Preferences` for testability
- `begin()` — opens NVS namespace `"runcam"`, writes defaults on first boot
  (detected via `"init2"` key) or after `resetToDefaults()`
- `load()` — reads all 13 settings with fallback to compile-time defaults
- `save()` — writes all settings atomically
- `saveSetting()` — writes a single setting with validation
- `resetToDefaults()` — overwrites all NVS keys with defaults from `config.h`

### transport/ — UART1

**`uart_transport.h/.cpp`** — Implements `IRunCamTransport` over HardwareSerial:
- `send()` — writes bytes and flushes the TX buffer
- `receive()` — waits up to `timeoutMs` for the first byte, then extends the
  deadline by `RUNCAM_INTER_BYTE_TIMEOUT_MS` (200 ms) after each subsequent
  byte. This handles the camera's bursty NAK retransmissions where multiple
  response frames arrive in quick succession.
- `flush()` — drains any pending RX bytes

### display/ — SSD1306 OLED

**`oled_display.h/.cpp`** — 128x32 SSD1306 over I2C (address 0x3C):
- `begin()` — initializes I2C, clears GDDRAM, pushes cleared display
- `render()` — called every 500 ms with current `SystemState`:
  - **IDLE + camera OK**: "IDLE" centered at size 2 (top), IP address centered
    at size 1 (bottom)
  - **IDLE + camera error**: "ERR!" centered at size 2
  - **RECORDING**: MM:SS counter centered at size 2 (top), "REC" centered at
    size 1 (bottom)

The display has a ~12-row COM offset: GDDRAM row 12 maps to physical row 0.
Text positions account for this offset (counter at y=12, label at y=2).

### web/ — REST API + WebSocket + UI

**`web_server.h/.cpp`** — ESPAsyncWebServer with REST endpoints:
- Each endpoint acquires `camMutex` via RAII `CamLock` before touching shared
  state
- Error responses include human-readable text (e.g. "camera rejected (wrong
  state)")
- Settings endpoints read/write NVS through `SettingsStore`

**`ws_notifier.h/.cpp`** — WebSocket at `/ws`:
- `broadcastStatus()` — pushes JSON status every 1 second to all connected
  clients: `{type: "status", state: "IDLE"|"RECORDING", recordingSeconds: N,
  cameraCommsOk: bool}`
- `broadcastError()` — pushes error events
- `cleanupClients()` — called each loop iteration to reap disconnected clients

**`web_ui.h`** — Generated by `scripts/generate_webui.py` at build time.
Contains the minified HTML as a PROGMEM string literal.

## REST API

### Status & Device

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Single-page web UI (PROGMEM) |
| GET | `/api/status` | State, recording seconds, auto-start setting, camera comms status |
| GET | `/api/device` | Protocol version, feature bitmask with named feature flags |

### Recording Control

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/record/start` | Start recording (idempotent) |
| POST | `/api/record/stop` | Stop recording (idempotent) |
| POST | `/api/photo` | Capture photo |

### Button Simulation

| Method | Endpoint | Body | Description |
|--------|----------|------|-------------|
| POST | `/api/button` | `{"button":"power"}` | Simulate power button press |
| POST | `/api/button` | `{"button":"mode"}` | Simulate mode button |
| POST | `/api/button` | `{"button":"wifi"}` | Simulate WiFi button |
| POST | `/api/button` | `{"button":"up\|down\|left\|right\|confirm"}` | 5-key OSD navigation |

### Settings

| Method | Endpoint | Body | Description |
|--------|----------|------|-------------|
| GET | `/api/settings` | — | Returns `{ok, autoStartRec}` |
| POST | `/api/settings/autoStartRec` | `{"value":0\|1}` | Set auto-start on boot |
| POST | `/api/settings/reset` | — | Reset all settings to defaults |

All POST endpoints return `{"ok":true}` on success or
`{"ok":false,"error":"description"}` on failure.

### Response Examples

**GET /api/status**
```json
{
  "state": "RECORDING",
  "recordingSeconds": 127,
  "cameraCommsOk": true,
  "autoStartRec": 1
}
```

**GET /api/device**
```json
{
  "protocolVersion": 1,
  "featureBitmask": "0x004F",
  "features": {
    "simulatePowerButton": true,
    "simulateWifiButton": true,
    "changeMode": true,
    "simulate5Key": false,
    "deviceSettingsAccess": false,
    "displayPort": false,
    "startRecording": false,
    "stopRecording": false
  }
}
```

## Web UI

Single-page app served from PROGMEM. Four tabs along the bottom:

- **Status** — Current state badge (IDLE blue / RECORDING red), recording time
  counter, camera communication status, auto-start setting display
- **Control** — Start/stop recording buttons, photo capture, button simulation
  panel (power, mode, WiFi, d-pad)
- **Settings** — Auto-start recording toggle (persists to NVS), reset to
  defaults button
- **Device** — Protocol version, feature bitmask with per-flag booleans

The UI auto-updates via WebSocket: recording time, state changes, and camera
status refresh without polling. Connection status indicator shows WebSocket
state (green = connected, red = disconnected).

## Settings

Stored in NVS (survives power cycles). Namespace: `"runcam"`.

| Setting | NVS Key | Default | Values |
|---------|---------|---------|--------|
| Resolution | `res` | 3 (1080p) | 0–4 |
| FPS | `fps` | 0 (120fps) | 0–3 |
| FOV | `fov` | 0 (Wide) | 0–2 |
| Video Format | `fmt` | 1 (PAL) | 0–1 |
| EIS | `eis` | 1 (enabled) | 0–1 |
| Loop Recording | `loop` | 0 (disabled) | 0–1 |
| Auto-Start Rec | `autostart` | 1 (enabled) | 0–1 |
| Sharpness | `sharp` | 1 (Medium) | 0–2 |
| Exposure | `exposure` | 0 (0 EV) | -2 to 2 |
| White Balance | `wb` | 0 (Auto) | 0–7 |
| Contrast | `contrast` | 1 (Medium) | 0–2 |
| Saturation | `saturation` | 1 (Medium) | 0–2 |
| Hue | `hue` | 0 | -180 to 180 |

Only `autoStartRec` is currently configurable from the web UI. Camera-side
setting IDs for the Thumb Pro W remain unknown; camera settings access is
deferred until vendor documentation is available.

## OLED Display

128x32 SSD1306 OLED on I2C (address 0x3C, GPIO5 SDA, GPIO6 SCL). Refreshes
every 500 ms.

| State | Top Line | Bottom Line |
|-------|----------|-------------|
| IDLE (camera OK) | `IDLE` (size 2, centered) | `192.168.4.1` (size 1, centered) |
| IDLE (camera error) | `ERR!` (size 2, centered) | — |
| RECORDING | `MM:SS` counter (size 2, centered) | `REC` (size 1, centered) |

The display module has a ~12-row COM offset: GDDRAM row 12 maps to physical
pixel row 0. Text Y-coordinates account for this (e.g., counter at GDDRAM y=12
appears at the physical top of the screen).

## Configuration

All compile-time constants are in `src/config.h`:

| Constant | Default | Purpose |
|----------|---------|---------|
| `WIFI_SSID` | `"RunCam-Controller"` | AP SSID |
| `WIFI_PASSWORD` | `"runcam1234"` | AP password |
| `WIFI_CHANNEL` | 6 | WiFi channel |
| `GPIO_UART_TX` | 3 | Camera UART TX pin |
| `GPIO_UART_RX` | 4 | Camera UART RX pin |
| `CAMERA_UART_NUM` | 1 | UART peripheral number |
| `GPIO_OLED_SDA` | 5 | OLED I2C data (hardwired) |
| `GPIO_OLED_SCL` | 6 | OLED I2C clock (hardwired) |
| `OLED_I2C_ADDR` | 0x3C | OLED I2C address |
| `OLED_WIDTH` | 128 | Display width in pixels |
| `OLED_HEIGHT` | 32 | Display height in pixels |
| `GPIO_STATUS_LED` | 8 | Onboard LED |
| `RUNCAM_BAUD_RATE` | 115200 | Camera UART baud rate |
| `RUNCAM_RESPONSE_TIMEOUT_MS` | 500 | Initial byte timeout |
| `RUNCAM_INTER_BYTE_TIMEOUT_MS` | 200 | Inter-byte timeout |
| `RUNCAM_MAX_RETRIES` | 3 | Send retry count |
| `NVS_NAMESPACE` | `"runcam"` | NVS namespace |
| `WS_STATUS_INTERVAL_MS` | 1000 | WebSocket push interval |
| `OLED_REFRESH_INTERVAL_MS` | 500 | OLED refresh interval |

## Testing

Host-side tests run on the development machine (no ESP32 required). Build with
`make test`.

### Test Files

| File | Tests | What it covers |
|------|-------|----------------|
| `test_protocol_encoding.cpp` | 6 | `buildFrame()` — header, command, data, CRC |
| `test_protocol_parsing.cpp` | 14 | `parseResponse()` — DeviceInfo, ACK, NAK, error cases |
| `test_crc8.cpp` | 5 | CRC8/DVB-S2 against known vectors |
| `test_camera.cpp` | 26 | RunCamCamera with MockTransport — begin, recording, buttons, settings, retry |
| `test_flight_controller.cpp` | 10 | FSM transitions, NAK surfacing, recording persistence |
| `test_settings_store.cpp` | 16 | NVS load/save/reset with MockPreferences |
| `test_settings_validation.cpp` | 12 | Setting value range validation |
| **Total** | **89** | |

### Test Infrastructure

- **Unity** test framework (tests/unity/)
- **MockTransport** — records sent bytes and returns pre-loaded responses
- **MockCamera** — tracks method calls and configurable return values
- **MockPreferences** — in-memory key-value store simulating NVS
- **Stubs** — minimal Arduino.h, HardwareSerial.h, WiFi.h for host compilation

Source files use `#ifndef UNIT_TEST` guards around ESP32-specific includes so
the same source compiles on both host and target.

## Dependencies

From `platformio.ini`:

| Library | Version | Purpose |
|---------|---------|---------|
| ESPAsyncWebServer | ^1.2.3 | Async HTTP + WebSocket server |
| AsyncTCP | ^1.1.1 | TCP transport for ESPAsyncWebServer |
| ArduinoJson | ^7.0.0 | JSON serialization for REST API |
| Adafruit SSD1306 | ^2.5.7 | SSD1306 OLED driver |
| Adafruit GFX Library | ^1.11.9 | Graphics primitives for OLED |

Build requires `ASYNCWEBSERVER_REGEX` flag (enabled in platformio.ini).

## Documentation

- **`runcam_esp32c3_fsd.md`** — Functional Spec (v1.1, corrected against hardware)
- **`runcam_thumb_pro_w_protocol.md`** — Protocol reference (corrected against hardware)
- **`Serial_Diagnostic_Report.md`** — Six iterations of on-the-wire probing
- **`Implementation_Plan_Phase_*.md`** — Phase plans
- **`Coder_Summary_Phase*_*.md`** — Run summaries
- **`Code_Review_Phase4_*.md`** — Code review of the redeveloped phase 4

## Status

Phases 1–4 implemented. Simplified on 2026-05-25 for screw-switch power
operation: removed flight-computer arm pin input, removed 5-minute auto-stop
timer, removed auto-restart, removed preflight checks. Added NVS-backed
auto-start recording setting with web UI toggle. OLED display corrected for
128x32 resolution with full GDDRAM clear to prevent power-on artifacts.

Camera setting IDs for the Thumb Pro W remain unknown; camera-side settings
access is deferred until vendor documentation is available.

Host test count: **89 / 89 passing**.
