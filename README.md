# RunCam Thumb ESP32-C3 Controller

ESP32-C3 based controller for a RunCam Thumb Pro W action camera. Powers on
via a screw switch, auto-starts recording on boot, exposes a WiFi web UI for
manual control, and shows recording status on a 0.42" OLED.

## Hardware

| Part | |
|---|---|
| MCU | ESP32-C3 OLED dev board (built-in 128x32 SSD1306, I2C on GPIO5/6) |
| Camera | RunCam Thumb Pro W |
| Power | 1S LiPo (3.5-4.2 V) via on/off screw switch |
| Camera UART | UART1, GPIO3 TX -> cam RX (white), GPIO4 RX <- cam TX (green) at 115200 8N1 |
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

## Project layout

```
src/
  protocol/        RCSP framing + CRC8/DVB-S2
  camera/          RunCamCamera facade (Power-toggle recording)
  flight/          FlightController FSM (IDLE / RECORDING)
  storage/         NVS-backed settings store
  transport/       UART1 IRunCamTransport
  display/         SSD1306 OLED render (128x32)
  web/             ESPAsyncWebServer REST + WS + single-page UI
  main.cpp         boot sequence + main loop
tests/             host-side Unity tests, build with `make test`
scripts/           HTML -> PROGMEM build pipeline
```

## Web UI

Four tabs:

- **Status** - current state (IDLE/RECORDING), recording time, camera comms status
- **Control** - start/stop recording, photo capture, button simulation
- **Settings** - auto-start recording on boot toggle, reset to defaults
- **Device** - camera protocol version, feature bitmask

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | State, recording seconds, auto-start setting, camera status |
| GET | `/api/device` | Protocol version, feature bitmask |
| POST | `/api/record/start` | Start recording |
| POST | `/api/record/stop` | Stop recording |
| POST | `/api/photo` | Capture photo |
| POST | `/api/button` | Simulate button (power/mode/wifi/d-pad) |
| GET | `/api/settings` | Get settings (autoStartRec) |
| POST | `/api/settings/autoStartRec` | Set auto-start (body: `{"value":0\|1}`) |
| POST | `/api/settings/reset` | Reset all settings to defaults |

## Settings

Stored in NVS (survives power cycles):

- **autoStartRec** (default: enabled) - automatically start recording after boot

## OLED Display

128x32 SSD1306 OLED. Shows:

- **IDLE** - "IDLE" label with WiFi IP address
- **RECORDING** - "REC" label with MM:SS counter, both centered
- **Camera error** - "ERR!" message

## Boot Sequence

1. USB CDC + status LED
2. NVS settings load
3. UART1 init + camera handshake (GET_DEVICE_INFO)
4. OLED init + full GDDRAM clear
5. Auto-start recording if enabled and camera OK
6. WiFi AP + web server start
7. Main loop: update state, render OLED, push WebSocket status

## Documentation

- **`runcam_esp32c3_fsd.md`** - Functional Spec (v1.1, corrected against hardware)
- **`runcam_thumb_pro_w_protocol.md`** - Protocol reference (corrected against hardware)
- **`Serial_Diagnostic_Report.md`** - Six iterations of on-the-wire probing
- **`Implementation_Plan_Phase_*.md`** - phase plans
- **`Coder_Summary_Phase*_*.md`** - run summaries
- **`Code_Review_Phase4_*.md`** - code review of the redeveloped phase 4

## Status

Phases 1-4 implemented. Simplified on 2026-05-25 for screw-switch power
operation: removed flight-computer arm pin input, removed 5-minute auto-stop
timer, removed auto-restart, removed preflight check. Added NVS-backed
auto-start recording setting with web UI toggle. OLED display corrected for
128x32 resolution with full GDDRAM clear to prevent power-on artifacts.

Camera setting IDs for the Thumb Pro W remain unknown; camera-side settings
access is deferred until vendor documentation is available.

Host test count: **89 / 89 passing**.
