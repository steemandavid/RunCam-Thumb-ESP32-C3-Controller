# Implementation Plan: Phase 4 — Redeveloped Against Corrected Protocol

**Date:** 2026-05-21 (v2 — same day re-do)
**Status:** Completed

## Phase Summary

Phase 4 was completed earlier today against an incorrect mental model of
the RunCam Device Protocol. Six iterations of on-the-wire testing
(`Serial_Diagnostic_Report.md`) exposed the gaps. The FSD has been
corrected to v1.1. This redevelopment makes the firmware match the
corrected FSD so that the camera actually responds.

## Requirements (FSD v1.1)

- §4.1 — request frame is `[0xCC][CMD][data][CRC]`, no LEN byte (already correct).
- §4.1.1 — response parser must dispatch on `0xCC` vs `0x55` first byte.
- §4.2 — CRC test vector `CC 00 → 0x60` (was `DB`).
- §4.4 — `GET_DEVICE_INFO` response is `[0xCC][proto][feat_lo][feat_hi][CRC]`. No model byte. `features` is `uint16_t`.
- §4.5 — settings access is deferred; endpoints return 503.
- §4.7 — `CAMERA_CONTROL` receives ACK/NAK; recording toggled via action `0x01` (Power), not `0x03`/`0x04`. NAK 0x02/0x04 are not retried.
- §5.1 — `FlightController` calls `startRecording()` / `stopRecording()` which both internally send action `0x01`.
- §5.2 — PreflightCheck runs in deferred mode (synthetic "settings access not available" result).
- §7.4 — Web UI Settings tab shows deferred banner; controls disabled.
- §7.5 — Device tab shows protocol version and features only (no model / firmware version).
- §8.5 — `/api/settings/*` endpoints return HTTP 503.
- §11.4 / §11.6 — test fixtures rewritten against real wire format.

## Files to Modify

| File | Change |
|---|---|
| `src/protocol/runcam_protocol.h` | `DeviceInfo` → drop modelId, `features` is `uint16_t`. Add `ResponseKind` enum and `ParsedResponse` struct. |
| `src/protocol/runcam_protocol.cpp` | Rewrite `parseResponse` to dispatch on first byte; rewrite `parseDeviceInfo` for `[CC][proto][feat_lo][feat_hi]` layout; add ACK/NAK parse path. |
| `src/camera/camera_result.h` | Add `REJECTED_STATE` and `REJECTED_ARG` for NAK 0x02 / 0x04. |
| `src/camera/runcam_camera.h` | `featureBitmask_` and `FEAT_*` → `uint16_t`. Track `isRecording_` boolean for Power-toggle semantics. |
| `src/camera/runcam_camera.cpp` | `startRecording()` and `stopRecording()` send action `0x01` (Power) and track state. `sendCommand` consumes ACK/NAK responses correctly. NAK 0x02/0x04 short-circuits retry. |
| `src/flight/preflight_check.h/.cpp` | Deferred mode — return synthetic result without calling camera. |
| `src/web/web_server.cpp` | `/api/device` drops `model`/`modelId`; uses 16-bit features. `/api/settings/*` return 503. |
| `src/web/index.html` | Settings tab disabled with banner. Device tab loses model/firmware. |
| `src/main.cpp` | Restore production boot sequence (currently diagnostic v26). |
| `tests/test_crc8.cpp` | Replace vectors with the 6 hardware-verified ones. |
| `tests/test_protocol_parsing.cpp` | Rewrite fixtures: 0xCC device-info, 0x55 ACK, 0x55 NAK, multi-frame buffer. |
| `tests/test_protocol_encoding.cpp` | Update if format assertions changed. |
| `tests/test_camera.cpp` | Update for uint16_t features, Power-toggle recording semantics, ACK/NAK responses in MockTransport queue. |
| `tests/test_preflight_check.cpp` | Replace with deferred-mode tests. |
| `tests/mocks/mock_transport.cpp` | No change expected. |

## Implementation Order

1. **Protocol layer** — `runcam_protocol.h` + `.cpp`. Smallest, highest leverage.
2. **Camera result codes** — add new enum values.
3. **Camera layer** — `runcam_camera.h` + `.cpp`. Depends on (1).
4. **Preflight layer** — deferred mode.
5. **Unit tests** — rewrite fixtures. Run `make test` until green.
6. **Web layer** — server + UI for deferred settings and updated device info.
7. **`main.cpp`** — restore production boot sequence using corrected modules.
8. **Build** — `pio run` to verify firmware compiles.
9. **Flash + hardware verify** — confirm GET_DEVICE_INFO, Mode, Photo, Power(toggle) all work on the camera.

## Testing Strategy

**Host tests (`make test`):** every protocol/camera/preflight change must
keep the unit test suite green. Test fixtures are rewritten to match
the real wire format, so the previous 78 passing tests will not all
pass unchanged.

**Firmware build (`pio run`):** must compile with no warnings, link
under the existing 1.3 MB flash budget.

**Hardware verification (`pio run --target upload` + capture):** flash
and observe the boot output. Must show:
- `GET_DEVICE_INFO` returns proto=v1, features=0x0077
- Recording toggle via Power button — log shows ACK 0x01
- Web server starts at 192.168.4.1
- No infinite retry loops on camera commands

## Risks / Open Questions

- **Settings deferred breaks the Web UI Settings tab** — by design.
  The tab is rendered but disabled.
- **Preflight deferred** — Status tab will show `preflight: { passed: true, deferred: true }`
  rather than the per-setting check results.
- **Camera state tracking via Power toggle is best-effort** — the
  ESP32 has no way to know if the camera state changed unexpectedly
  (e.g. user pressed the physical button). LED / current sensing is
  the out-of-band confirmation per the protocol doc §11.
- **Action 0x03 / 0x04** kept in the code as `triggerStartRecordingExplicit()`
  / `triggerStopRecordingExplicit()` for completeness, but unused by
  the flight controller. May be useful for diagnostics.
