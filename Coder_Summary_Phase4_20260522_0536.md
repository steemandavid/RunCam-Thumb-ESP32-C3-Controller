# Coder Run Summary: Phase 4 (re-developed)

**Date:** 2026-05-22 05:36
**Phase / Feature:** Phase 4 — Final integration, redeveloped against
the protocol corrections from `Serial_Diagnostic_Report.md`
**Plan File:** `Implementation_Plan_Phase_4.md`

## What Was Implemented

- **Protocol layer rewritten** to dispatch responses on the first byte:
  `0xCC` for GET_DEVICE_INFO, `0x55` for ACK/NAK. `DeviceInfo` now
  carries proto + `uint16_t` features (no model byte). Added a
  `ParsedResponse` discriminated union exposing kind / actionEcho /
  errCode.
- **Camera layer** uses the new parser. Recording is toggled via
  action `0x01` (Power button) with host-tracked `isRecording_`
  state. NAK responses (0x01 / 0x02 / 0x04) map to new
  `CameraResult::REJECTED_*` codes and short-circuit retries
  (transient errors still retry up to `RUNCAM_MAX_RETRIES`). Feature
  constants are `uint16_t`.
- **PreflightCheck** in deferred mode — returns a synthetic
  `passed=true, deferred=true` result without contacting the camera,
  matching FSD §5.2.
- **Web layer** `/api/device` no longer reports model/firmware (they
  are not on the wire). `/api/settings/*` returns HTTP 503. The Web
  UI's Settings tab is replaced by a "Settings unavailable" banner.
  Status preflight badge becomes "N/A" in deferred mode.
- **Production `main.cpp`** restored (was diagnostic v26). Boot
  sequence is the FSD §12.4 order.
- **Tests rewritten** against the verified wire format. CRC vectors,
  parse cases (DeviceInfo, ACK, NAK, multi-frame buffers), camera
  recording toggle, NAK no-retry, settings deferred — all covered.

## Files Created / Modified

| File | Change |
|---|---|
| `src/protocol/runcam_protocol.h` | Rewrite: `ResponseKind`, `ParsedResponse`, uint16_t features, no modelId |
| `src/protocol/runcam_protocol.cpp` | Rewrite: dispatch on first byte; ACK/NAK parsing |
| `src/camera/camera_result.h` | Add `REJECTED_STATE`, `REJECTED_ARG`, `REJECTED_UNKNOWN` |
| `src/camera/runcam_camera.h` | uint16_t features, `isRecording_`, `simulateWifiButton`, etc. |
| `src/camera/runcam_camera.cpp` | Power-toggle recording, NAK no-retry, new sendRequest path |
| `src/flight/preflight_check.h` | Add `deferred` field |
| `src/flight/preflight_check.cpp` | Deferred-mode implementation |
| `src/web/web_server.cpp` | `/api/device` without modelId; settings → 503 |
| `src/web/index.html` | Settings tab disabled banner; device tab shows protocol+bitmask |
| `src/main.cpp` | Production boot sequence restored from diagnostic v26 |
| `tests/test_crc8.cpp` | 12 vectors verified on hardware |
| `tests/test_protocol_parsing.cpp` | 19 tests covering both response formats and multi-frame buffer |
| `tests/test_camera.cpp` | 18 tests using ACK/NAK fixtures and recording toggle semantics |
| `tests/test_preflight_check.cpp` | 3 deferred-mode tests |

## Test Results

Host test suite — `make test`:

```
test_camera                18 Tests   0 Failures
test_crc8                  12 Tests   0 Failures
test_flight_controller      9 Tests   0 Failures
test_preflight_check        3 Tests   0 Failures
test_protocol_encoding     13 Tests   0 Failures
test_protocol_parsing      19 Tests   0 Failures
test_settings_store         7 Tests   0 Failures
test_settings_validation    8 Tests   0 Failures
                          ----------------
                           89 Tests   0 Failures
ALL TESTS PASSED
```

Firmware build — `pio run`:

```
RAM:   13.8% (45 356 / 327 680 bytes)
Flash: 84.9% (1 113 084 / 1 310 720 bytes)
SUCCESS in 4.5 s
```

Hardware boot — flashed and captured via USB CDC:

```
=== RunCam Thumb Pro W Controller boot ===
Settings: loaded from NVS
Camera: proto=v1 features=0x0077          <-- correct parse
Preflight: deferred (settings access unavailable)
OLED: ready
WiFi AP: SSID="RunCam-Controller" IP=192.168.4.1
=== boot complete ===
```

The camera is correctly identified (proto v1, features 0x0077 = bits
0, 1, 2, 4, 5, 6) on the very first GET_DEVICE_INFO, validating the
new parser against live hardware. No retry timeouts, no header errors.

## Deviations from the Plan

- **Removed `apiNameToSettingId` and unused setting-name lookup tables**
  from `web_server.cpp` once the settings endpoints became 503-only.
  Not in the plan but a natural follow-on cleanup.
- **`web_ui.h` em-dash narrowing fix:** my deferred-banner text used
  an em-dash that broke the generated `const char[]` PROGMEM array
  (narrowing 8212 to char). Replaced with ASCII hyphen.

## Outstanding Work / Follow-ups

- **Manual record/stop verification via web UI**: the boot proves the
  CC-format parser works end-to-end; the 0x55-format ACK/NAK path is
  covered comprehensively by host tests against the exact bytes
  captured from the camera. A full WiFi-connected web-UI smoke test
  (start rec, observe LED, stop rec) would close the loop but
  requires a phone or laptop joining the AP — manual.
- **Setting-ID map (deferred)**: the Web UI Settings tab is a
  no-op until the Thumb Pro W's setting-ID map is documented or
  reverse-engineered. The validator + NVS layer are kept tested and
  ready to re-enable.
- **Diagnostic v26 sketch** is no longer in `src/main.cpp`. If
  future hardware debugging is needed, it can be retrieved from git
  history once committed, or stored under `scripts/`.
