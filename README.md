# RunCam Thumb ESP32-C3 Controller

ESP32-C3 based controller for a RunCam Thumb Pro W action camera. Auto-starts
recording on an arm signal from a flight computer, exposes a WiFi web UI for
manual control, and shows status on a 0.42" OLED. Built and verified against
a physical Thumb Pro W on 2026-05-21.

## Hardware

| Part | |
|---|---|
| MCU | ESP32-C3 OLED dev board (built-in 72x40 SSD1306, I²C on GPIO5/6) |
| Camera | RunCam Thumb Pro W |
| Power | 1S LiPo (3.5–4.2 V) |
| Camera UART | UART1, GPIO3 TX → cam RX (white), GPIO4 RX ← cam TX (green) at 115200 8N1 |
| Arm input | GPIO2, active-low with internal pull-up |
| Status LED | GPIO8 (onboard) |

## Build / Flash

```bash
# Host unit tests
make test                # 89/89 tests pass against wire-verified fixtures

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
  flight/          ARM-pin debounce, FSM, preflight (deferred)
  storage/         NVS-backed settings store
  transport/       UART1 IRunCamTransport
  display/         SSD1306 OLED render
  web/             ESPAsyncWebServer REST + WS + single-page UI
  main.cpp         boot sequence + main loop
tests/             host-side Unity tests, build with `make test`
scripts/           HTML → PROGMEM build pipeline
```

## Documentation

- **`runcam_esp32c3_fsd.md`** — Functional Spec (v1.1, corrected against hardware)
- **`runcam_thumb_pro_w_protocol.md`** — Protocol reference (corrected against hardware)
- **`Serial_Diagnostic_Report.md`** — Six iterations of on-the-wire probing that
  exposed the protocol mistakes in the v1.0 docs. Includes the verified
  response formats (`[0xCC]` for device info, `[0x55]` ACK/NAK for everything
  else) and the empirically-confirmed CRC vectors.
- **`Implementation_Plan_Phase_*.md`** — phase plans
- **`Coder_Summary_Phase*_*.md`** — run summaries
- **`Code_Review_Phase4_*.md`** — code review of the redeveloped phase 4

## Status

Phases 1–4 implemented and integrated. The protocol layer was rewritten on
2026-05-22 to match the actual on-the-wire behaviour of the Thumb Pro W
(different from what the original spec doc described). The camera correctly
responds to `GET_DEVICE_INFO` reporting proto v1, features `0x0077`, and
button simulation commands ACK/NAK as documented.

**Deferred** until vendor docs are available:

- Camera settings access — the Thumb Pro W's setting-ID map is not documented;
  `/api/settings/*` endpoints return HTTP 503 and the Settings tab in the
  web UI shows an "unavailable" banner.
- PreflightCheck — runs in deferred mode (synthetic pass result) for the
  same reason; the LED never enters the "preflight failed" pattern.

**Code review** (`Code_Review_Phase4_20260522_0537.md`) flagged 3 MAJOR
and 5 MINOR findings; all were addressed in commit `ac49034`. The
firmware now:

- Extends the UART receive deadline by 200 ms after each byte to handle
  the camera's bursty NAK retransmissions (FSD §4.7).
- Serialises all camera + flight controller access through a FreeRTOS
  mutex so AsyncTCP web callbacks cannot race against the main loop.
- Backs off auto-stop retries to 1 Hz and gives up after 5 attempts
  rather than spinning on a persistent `REJECTED_STATE` NAK.
- Surfaces NAK rejections through the REST API (`/api/record/start`,
  `/api/photo`, `/api/button`, etc.) instead of falsely reporting
  `ok:true`.

Host test count: **94 / 94 passing**.
