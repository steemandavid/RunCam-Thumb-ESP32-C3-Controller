# Phase 4 Code Review — Final Integration (Re-developed Against Corrected Protocol)

**Document ID:** RUNCAM-REVIEW-P4-002
**Reviewer:** Code Review Agent
**Date:** 2026-05-22
**Scope:** Phase 4 — final integration as redone after the on-the-wire
diagnostic (`Serial_Diagnostic_Report.md`) exposed protocol mismatches
in the v1.0 FSD. The first Phase 4 (review document
`Coder_Summary_Phase4_20260521_2047.md`) is superseded.
**FSD Reference:** `runcam_esp32c3_fsd.md` v1.1
**Protocol Reference:** `runcam_thumb_pro_w_protocol.md` (corrected
2026-05-21)
**Diagnostic Reference:** `Serial_Diagnostic_Report.md`
**Implementation Plan:** `Implementation_Plan_Phase_4.md` (status: Completed)
**Commit Reviewed:** working tree (project is not yet under git)

---

## Verdict: **PASS WITH NOTES**

The redevelopment correctly implements every protocol correction
identified in `Serial_Diagnostic_Report.md`. All 89 host tests pass
using wire-verified vectors; the firmware boots on real hardware and
correctly decodes `GET_DEVICE_INFO` as proto v1 / features 0x0077.
The deferred items (settings access, preflight) are clearly fenced
off in code and documented in the FSD.

The notes below cover **pre-existing** issues that survived the
redevelopment (web↔flight task synchronisation, misleading REST
response shapes, stale WebSocket payload fields). None are blockers
for proceeding, but they should be tracked.

---

## Table of Contents

1. [Coverage Analysis](#1-coverage-analysis)
2. [Deviation Report](#2-deviation-report)
3. [Plan vs. Implementation](#3-plan-vs-implementation)
4. [Edge Cases & Safety](#4-edge-cases--safety)
5. [Concurrency & Platform Issues](#5-concurrency--platform-issues)
6. [Error Handling](#6-error-handling)
7. [Code Quality](#7-code-quality)
8. [Summary](#8-summary)
9. [Recommendation](#9-recommendation)

---

## Files Reviewed

| File | Purpose |
|------|---------|
| `src/protocol/runcam_protocol.h` | Public types, command constants, parse result codes |
| `src/protocol/runcam_protocol.cpp` | Frame build, response dispatch (`0xCC` vs `0x55`) |
| `src/camera/camera_result.h` | New `REJECTED_*` codes |
| `src/camera/runcam_camera.h` | Camera facade interface |
| `src/camera/runcam_camera.cpp` | Power-toggle recording, NAK handling, retries |
| `src/flight/preflight_check.h/.cpp` | Deferred mode |
| `src/web/web_server.cpp` | REST routes, settings → 503 |
| `src/web/index.html` | Web UI with deferred settings tab |
| `src/web/ws_notifier.cpp` | WebSocket broadcast (pre-existing) |
| `src/display/oled_display.cpp` | OLED render based on SystemState (pre-existing) |
| `src/main.cpp` | Restored production boot sequence |
| `tests/test_crc8.cpp` | CRC vectors verified on hardware |
| `tests/test_protocol_parsing.cpp` | Both response formats, multi-frame |
| `tests/test_protocol_encoding.cpp` | Frame build assertions |
| `tests/test_camera.cpp` | Camera-layer behaviour, ACK/NAK, Power-toggle |
| `tests/test_preflight_check.cpp` | Deferred-mode tests |

---

## 1. Coverage Analysis

| FSD Requirement | Section | Status | Evidence |
|---|---|---|---|
| Request frame is `[0xCC][CMD][data][CRC]`, no LEN | §4.1 | DONE | `runcam_protocol.cpp::buildFrame` |
| Dispatch parse on `0xCC` vs `0x55` first byte | §4.1.1 | DONE | `runcam_protocol.cpp::parseResponse` lines 19–55 |
| `GET_DEVICE_INFO` response = `[CC][proto][feat_lo][feat_hi][CRC]` | §4.4 | DONE | parseResponse `0xCC` branch + test fixtures |
| `featureBitmask` is `uint16_t`; no `modelId` byte | §4.4 | DONE | `DeviceInfo` struct |
| ACK / NAK 0x55-framed parse with length-prefix | §4.1.1 | DONE | `0x55` branch — handles `len=5` NAK and `len=6` ACK |
| NAK error codes 0x01 / 0x02 / 0x04 mapped | §4.1.1 | DONE | `NAK_*` constants + `nakToResult` |
| CRC8/DVB-S2 algorithm | §4.2 | DONE (unchanged) | `protocol/crc8.cpp` |
| Test vector `CC 00 → 0x60` | §4.2 | DONE | `tests/test_crc8.cpp::test_crc8_request_get_device_info` |
| `CAMERA_CONTROL` reads ACK/NAK reply | §4.7 | DONE | `sendRequest` consumes one frame |
| Recording via action `0x01` (Power) | §4.7 | DONE | `startRecording()` / `stopRecording()` |
| NAK 0x02 / 0x04 not retried | §4.7 | DONE | `sendRequest` returns immediately on NAK |
| Timeout / CRC retried up to MAX_RETRIES | §4.7 | DONE | retry loop in `sendRequest` |
| Settings access deferred — endpoints return 503 | §4.5, §8.5 | DONE | `web_server.cpp` lines 122–135 |
| Preflight deferred mode returns synthetic result | §5.2 | DONE | `preflight_check.cpp` |
| Web UI Settings tab disabled with banner | §7.4 | DONE | `src/web/index.html` |
| Device tab without model / fw version | §7.5 | DONE | `index.html` + `/api/device` |
| Flight FSM uses Power-toggle internally | §5.1 | DONE | `RunCamCamera::startRecording` |
| Boot sequence: NVS → camera → preflight → WiFi → web | §12.4 | DONE | `main.cpp::setup` |
| OLED reflects FlightState | §11.4 | DONE (pre-existing) | `oled_display.cpp` |
| Unit tests rewritten against wire-verified fixtures | §11.4 | DONE | 89/89 pass |
| Inter-byte receive timeout ≥ 200 ms | §4.7 | **PARTIAL** | The recv loop uses a single `RUNCAM_RESPONSE_TIMEOUT_MS` budget; there is no separate inter-byte timeout. See finding **MAJOR-1**. |

Twenty-one of twenty-two requirements are fully covered; one is
partial (note below).

---

## 2. Deviation Report

### MAJOR

**MAJOR-1 — No inter-byte timeout on response read.**
FSD §4.7 says: "Once any byte arrives, the receive continues with an
**inter-byte timeout of 200 ms** to handle bursty replies." The
production code uses a single overall budget in
`UartTransport::receive` (`(millis() - start) < timeoutMs`). It does
not extend the deadline as bytes arrive, so a slow first byte plus a
later burst can run past 500 ms and time out mid-frame.

In practice the camera responds well within 500 ms on the bench, so
this has not bitten the boot path. But under load or when reading
multi-frame NAK bursts (`Serial_Diagnostic_Report.md` §7.4) the
parser may see a truncated buffer and force an unnecessary retry.

The diagnostic sketch (v26, since overwritten) implemented this
correctly. Re-introduce inter-byte handling in `UartTransport::receive`:

```cpp
uint32_t deadline = millis() + firstTimeoutMs;
while (idx < maxLen && (int32_t)(deadline - millis()) > 0) {
    int b = serial_.read();
    if (b >= 0) {
        buffer[idx++] = (uint8_t)b;
        deadline = millis() + interByteTimeoutMs;  // <-- this
    }
}
```

Severity: **MAJOR** because it materially affects the protocol's
ability to handle the camera's documented burst-retransmission
behaviour. **It is not Critical** because the host tests pass and the
hardware boot path completes successfully today.

### MINOR

**MINOR-1 — `WsNotifier::broadcastStatus` carries stale fields.**
`src/web/ws_notifier.cpp:18–19`:
```cpp
doc["autoRestart"] = false;
doc["armPin"]      = false;
```
Both are hardcoded `false` regardless of actual state. `/api/status`
returns the live values; the WebSocket push lies. Pre-existing from
Phase 4 v1 — not introduced by this redev — but worth fixing while
touching the file.

**MINOR-2 — `WsNotifier::broadcastPreflight` still uses the
non-deferred shape.** Its `checks` object includes resolution/fps/eis
detail (`ws_notifier.cpp:31–39`) but `/api/status` now publishes the
`deferred` flag instead. Two endpoints should describe preflight the
same way. Either remove the `checks` object from the WS payload or
add a `deferred` flag and consistent shape.

**MINOR-3 — Misleading REST responses on failure.**
`/api/record/start`, `/api/record/stop`, `/api/arm`, `/api/disarm`
unconditionally return `{"ok":true,...}` regardless of what
`fc_->forceStartRecording()` actually did (`web_server.cpp:66–110`).
With the new `REJECTED_STATE` result code, the camera can refuse a
recording start and the flight controller silently swallows it.
Recommendation: have `forceStartRecording()` return a `CameraResult`
and surface it.

**MINOR-4 — `/api/arm` returns `state:"ARMED"` but the flight
controller jumps directly to RECORDING.** There is no `ARMED` state
in the actual FSM transition (`FlightController::forceStartRecording`
goes IDLE → RECORDING). The response shape misleads the UI.
Pre-existing.

**MINOR-5 — Power-toggle assumes the host knows the initial
recording state.** `RunCamCamera::isRecording_` starts `false`
(`runcam_camera.cpp` construction). If the camera was left recording
across an ESP32 reset (e.g. an unattended-flight scenario), the first
"start" press will actually *stop* recording without the host
realising. FSD §4.7 acknowledges this as out-of-band; the LED / current
draw is the canonical signal. Worth adding a `LOG_WARN` if a
recording is rejected as `REJECTED_STATE` ("possible state drift").

### INFO

- The `mock_camera.h` still exposes `readSettingFail` /
  `resolutionValue` / `fpsValue` / `eisValue` fields used only by
  the previous preflight tests. Harmless but dead. Consider trimming.
- `preflight_check.cpp` takes `ICamera&` by reference and never
  uses it. Could be a `static` free function or take by pointer +
  null. Acceptable as a stub for restoration to the original
  behaviour.

---

## 3. Plan vs. Implementation

The implementation plan is `Implementation_Plan_Phase_4.md`
(Status: Completed).

| Plan Item | Planned | Actual | Status |
|---|---|---|---|
| Rewrite `runcam_protocol.h/.cpp` for dispatch | Yes | Yes | ✓ |
| Add `REJECTED_*` to `camera_result.h` | Yes | Yes | ✓ |
| Camera: uint16_t features, Power-toggle, NAK no-retry | Yes | Yes | ✓ |
| `PreflightCheck` deferred mode | Yes | Yes | ✓ |
| `/api/settings/*` returns 503 | Yes | Yes | ✓ |
| `/api/device` without modelId/firmware | Yes | Yes | ✓ |
| `index.html` settings tab disabled banner | Yes | Yes | ✓ |
| Restore production `main.cpp` | Yes | Yes | ✓ |
| Tests rewritten with wire-verified fixtures | Yes | Yes (89/89 pass) | ✓ |
| Implementation order: proto → result → camera → preflight → tests → web → main → build → hardware | Yes | Followed exactly | ✓ |
| Testing strategy: host tests green, firmware build, hardware boot | Yes | All three completed | ✓ |

**Undocumented deviations** (called out in `Coder_Summary_Phase4_20260522_0536.md`):

- `web_server.cpp`: removed the now-unused `apiNameToSettingId` helper and
  the setting-name lookup tables (`resNames`, `fpsNames`, etc.). Justified
  cleanup — these were dead after the settings endpoints became 503-only.
- `index.html`: an em-dash in the deferred banner triggered a
  `-Wnarrowing` error in the generated PROGMEM `const char[]`.
  Replaced with ASCII hyphen. Trivial, documented.

No surprises. Plan adherence is good.

---

## 4. Edge Cases & Safety

**Power-fail mid-recording (Safety / pre-existing):** ESP32 reset
during a recording leaves the camera recording but the new
RunCamCamera instance believes it is not. First user-initiated Start
will toggle Power and stop the recording. Per FSD §4.7 this is
acknowledged as out-of-band; the LED is the canonical signal.
**Mitigation**: at boot, after `GET_DEVICE_INFO`, send a Power button
press if `autoStartRecording` is off, so the camera ends in standby
regardless of prior state. (Not in scope for this review — proposing
as a Phase 5 candidate.)

**Camera not connected at boot:** `RunCamCamera::begin()` returns
`ERROR_TIMEOUT`. `initialised_` stays `false`. Subsequent commands
return `ERROR_NOT_INITIALISED`. The status LED keeps blinking;
`/api/status` reports `cameraCommsOk: false`. **Safe** — system does
not lock up, does not falsely report success.

**SD card error (fast-blink red LED):** Camera responds to
`GET_DEVICE_INFO` normally, but `CAMERA_CONTROL` action 0x01 returns
NAK 0x02. With the new no-retry behaviour, this surfaces fast as
`REJECTED_STATE`. **Safe** — visible to the operator via LED and via
the new result code. The misleading `ok:true` REST response (MINOR-3)
prevents the UI from communicating this; fixing MINOR-3 closes that gap.

**Auto-stop timer expiry:** `FlightController::update()` calls
`stopRecording()` after 5 minutes (`flight_controller.cpp:38`). The
new `stopRecording()` only sends Power if `isRecording_` is true, so
back-to-back expiry doesn't double-toggle. If the Power command
returns `REJECTED_STATE`, `isRecording_` stays true — `update()` will
keep trying every loop iteration. **Bug potential**: a sustained NAK
of Power could thrash the camera with hundreds of attempts per
second. Add a back-off (try at most once per second after a NAK).
Severity MINOR.

**Concurrent Web + Loop access:** see §5.

---

## 5. Concurrency & Platform Issues

**Pre-existing — shared state without protection.**
ESPAsyncWebServer callbacks (`/api/record/start` etc.) run on the
AsyncTCP task. `loop()` runs on the Arduino task. Both access
`FlightController` state (`fc_->forceStartRecording()` from the
web task vs `fc_->update()` from `loop()`) and `RunCamCamera` state
(`isRecording_`, transport buffers) without any mutex.

The shared state is small and the operations are short, so the
practical failure mode is rare on a single-core RISC-V part — but
`fc_->update()` doing a Power toggle while a web request also calls
`forceStartRecording()` could cause two Power presses in quick
succession and leave `isRecording_` desynced.

Recommendation: wrap each `FlightController` and `RunCamCamera` call
in a critical section or a single mutex held for the entire
high-level operation. Pre-existing item from Phase 4 v1; flagged
again here.

**Severity: MAJOR** for a flight-critical product. Listed as MAJOR-2
in the deviation summary.

**UART access** is single-threaded (only `loop()` calls it via
`FlightController` and via the web routes that also funnel through
`FlightController`). With the mutex above, this is fine.

**OLED I²C** is only touched from `loop()` (`oled.render()`). Fine.

**WiFi/WebSocket** is handled by AsyncTCP — out of scope.

No interrupts are used in this firmware. No ISR / task interaction
to worry about.

---

## 6. Error Handling

**Strong points:**
- `RunCamCamera::sendRequest` propagates `ParseResult` into
  `CameraResult` cleanly (`parseResultToCameraResult`).
- NAK responses no longer wedge the retry loop — immediate return.
- Initialisation gate (`!initialised_`) on every public method
  prevents undefined behaviour when the camera never came up.
- `OledDisplay::begin()` returns `bool` and `main.cpp` logs failure.

**Gaps:**
- `MINOR-3` above — web REST endpoints discard camera results.
- `FlightController::update()` discards the return value of
  `stopRecording()` on auto-stop expiry (`flight_controller.cpp:39`).
- `WsNotifier::broadcastError()` exists but is never called from
  anywhere. Plumbing for camera comms errors → WS push is missing.
  Pre-existing.

---

## 7. Code Quality

**Strong points:**
- Naming is consistent with earlier phases (`sendRequest`,
  `parseResponse`, `nakToResult`).
- Header / impl separation is clean. New types
  (`ResponseKind`, `ParsedResponse`) are well-scoped.
- Comments are sparse and meaningful — they say *why* (e.g. "Power-
  toggle per FSD §4.7", "best-effort, see FSD §4.5") rather than
  what.
- Tests use small, declarative builder helpers
  (`deviceInfo()`, `ack()`, `nak()`) that document the wire format
  by example.
- No dead code introduced; the deletion of the settings name tables
  is a tidy follow-on.

**Minor observations:**
- `RunCamCamera::readSetting()` returns `outValue = resp.actionEcho`
  on ACK. That's documented in code as best-effort; if the settings
  protocol turns out not to ACK at all, this is misleading. Could
  return `REJECTED_UNKNOWN` instead until the real format is known.
- The protocol's `frameLength` field on `ParsedResponse` is set but
  not consumed in the firmware (only `tests/test_protocol_parsing.cpp::test_parse_two_naks_queued`
  uses it). Useful for future multi-frame handling — keep it.

No code-quality red flags.

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|---|---|---|---|---|
| Spec conformance | 0 | 0 | 0 | 0 |
| Plan conformance | 0 | 0 | 0 | 2 |
| Correctness | 0 | 1 (auto-stop NAK loop) | 1 (readSetting return) | 0 |
| Safety | 0 | 0 | 1 (power-fail state drift) | 0 |
| Concurrency | 0 | 1 (no mutex on shared state) | 0 | 0 |
| Error handling | 0 | 1 (no inter-byte timeout) | 3 (REST results, WS error, WS preflight shape) | 0 |
| Code quality | 0 | 0 | 0 | 2 |
| **Totals** | **0** | **3** | **5** | **4** |

The three **MAJOR** items are:

1. UART receive lacks the FSD-specified inter-byte timeout (deviation from §4.7).
2. Shared state between `loop()` and AsyncTCP web callbacks is not
   protected by a mutex.
3. Auto-stop expiry will retry `stopRecording()` indefinitely if the
   camera persistently NAKs `REJECTED_STATE`.

None of these blocks acceptance — the camera responds correctly, the
firmware boots, the host test suite is comprehensive and green. They
should be addressed before flight-critical use.

---

## 9. Recommendation

**Pass to proceed to GitHub commit (`/github`).**

Before flying the system, address the three MAJOR findings:

1. Add the inter-byte timeout to `UartTransport::receive` to match
   FSD §4.7. About 5 lines.
2. Wrap `FlightController` and `RunCamCamera` operations in a mutex
   (FreeRTOS `portMUX_TYPE` or `std::mutex`). About 20 lines.
3. Add a back-off in `FlightController::update()` when auto-stop
   `stopRecording()` returns `REJECTED_*` — retry at most once per
   second.

The MINOR findings (REST response truthfulness, WebSocket payload
consistency, state-drift logging) are nice-to-fix follow-ups that
can be batched into a "Phase 5: Robustness" cleanup.

The implementation matches the corrected FSD section-for-section,
the host test suite is comprehensive against the verified wire
format, and the hardware confirms the protocol layer is now
correct. This phase is complete.
