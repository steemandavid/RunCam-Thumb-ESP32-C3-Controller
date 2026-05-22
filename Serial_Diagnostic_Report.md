# Serial Diagnostic Report — RunCam Thumb Pro W

**Date:** 2026-05-21
**Camera under test:** RunCam Thumb Pro W (proto v1, features `0x0077`)
**Host:** ESP32-C3 OLED dev board, UART1 GPIO3 TX / GPIO4 RX, 115200 8N1
**Method:** Six iterations of a diagnostic sketch flashed to the ESP32-C3,
captured over USB CDC. Every TX and RX byte logged. Findings below are
**ground-truth from the actual hardware**, not from the spec.

---

## TL;DR

1. **The wiring is fine** — camera responds to `GET_DEVICE_INFO` reliably.
2. **The protocol spec doc and the FSD are both wrong** in multiple places.
3. **The production firmware's protocol layer needs to be rewritten** —
   not just patched. The actual on-the-wire protocol is meaningfully
   different from what the FSD and the FSD-derived unit tests describe.
4. The bug catalogue from my earlier (spec-only) report was **partly right
   but partly wrong** — I have corrected it below.

---

## 1. What the camera actually does on the wire

### 1.1 Request frame (host → camera) — unchanged from spec

```
[0xCC] [CMD] [data...] [CRC8/DVB-S2 over preceding bytes]
```

No length byte. The FSD §4.1 diagram with `[0xCC][CMD][LEN][DATA][CRC]`
is wrong — the actual implementation in `runcam_protocol.cpp::buildFrame`
already omits the `LEN` byte, so outgoing commands are correctly framed.

### 1.2 Response — `GET_DEVICE_INFO` (CMD 0x00)

```
[0xCC] [proto] [feat_lo] [feat_hi] [CRC8 over preceding 4 bytes]
```

5 bytes total. Verified on hardware:

```
TX:  CC 00 60
RX:  CC 01 77 00 02
     │  │  │  │  └── CRC = DVB-S2 over CC 01 77 00 = 0x02 ✓
     │  │  └──┴────── features uint16_t LE = 0x0077
     │  └──────────── protocol version = 1
     └─────────────── header
```

**Important: the protocol-spec doc test vector `CC 00 DB` is wrong.**
The correct DVB-S2 CRC over `CC 00` is `0x60`. The other five vectors in
the same table all check out — only this one has a typo. Verified
independently with a Python implementation; all six match my embedded C
implementation exactly.

There is **no** "model id" byte in this response. The FSD §4.4 ("response
also includes a camera model identifier (1 byte)") is wrong. The 3-byte
payload is `proto, feat_lo, feat_hi`.

### 1.3 Response — every other command

Completely different format from the spec — uses header **`0x55`**, has
a **length byte**, and carries a status code:

```
ACK:  [0x55] [0x06] [0x01] [0x00] [echoed_action] [CRC]      ← 6 bytes
NAK:  [0x55] [0x05] [0xFF] [err_code]             [CRC]      ← 5 bytes
```

CRC is **the same** CRC-8/DVB-S2 over the whole frame including the
`0x55`. Confirmed against five captured ACKs and NAKs — all match.

### 1.4 Observed NAK error codes

| Code | Meaning (empirically) |
|---|---|
| `0x01` | unknown command / wrong CRC — returned for bytes that don't parse |
| `0x02` | wrong state for command — e.g. `START_RECORDING` while already recording |
| `0x04` | invalid argument / unsupported ID — e.g. `GET_SETTINGS` for an unknown setting ID |

### 1.5 The protocol spec doc claim that "responses have no leading byte" is wrong

It is wrong for **both** response formats. `GET_DEVICE_INFO` responses
start with `0xCC`; everything else starts with `0x55`. There is no
response format that omits a header byte.

---

## 2. Per-command behaviour, observed

### 2.1 `GET_DEVICE_INFO` (CMD 0x00)

Reliable. Returns 5-byte CC-format response on every attempt.
Features bitmask is `0x0077` = bits **0, 1, 2, 4, 5, 6**:

```
bit 0  POWER_BUTTON     ✓
bit 1  WIFI_BUTTON      ✓
bit 2  CHANGE_MODE      ✓
bit 3  5_KEY_OSD        ✗  (consistent with Thumb Pro W lacking 5-key cable)
bit 4  SETTINGS_ACCESS  ✓  (advertised — but see §2.4 caveat)
bit 5  DISPLAYPORT      ✓
bit 6  START_RECORDING  ✓  (advertised — but action 0x03 never ACKs, see §2.2)
bit 7  STOP_RECORDING   ✗
```

The fact that bit 7 is not set means there is no explicit STOP command —
recording is stopped by the **Power button (action 0x01) toggling** the
recording state.

### 2.2 `CAMERA_CONTROL` (CMD 0x01)

Tested for every documented action. **Every action receives a 5- or
6-byte response — this is NOT fire-and-forget**, contrary to the spec.

| Action | Name | Observed |
|---|---|---|
| `0x00` | WiFi / Confirm | ACK or NAK 0x02 depending on camera mode |
| `0x01` | Power button | **ACK every time** — toggles record state |
| `0x02` | Mode / Exit | **ACK every time** — cycles Video → Photo → QR |
| `0x03` | Start Recording | **NAK 0x02 every time** in our tests — see below |
| `0x04` | Stop Recording | ACK when in a state where stop makes sense |
| `0x05` | Capture Photo | **ACK every time** |

**About action 0x03 (Start Recording):** the feature bit advertises it,
but in five independent test runs across four mode-cycle positions, it
**always** returned NAK 0x02 ("wrong state"). The simplest hypothesis
is that when these tests ran, the camera was either already recording
(slow-blink red — confirmed by the user) or in a non-Video mode, and
action 0x03 is only valid in Video mode with recording stopped.

**The pragmatic conclusion for the production firmware:** **stop using
0x03 / 0x04 to toggle recording.** Use action `0x01` (Power button) for
both start and stop — it always ACKs and the camera handles the toggle
internally. This matches how the camera's hardware button works.

### 2.3 `5KEY_SIMULATION_PRESS` (CMD 0x02)

Sends produce **no response whatsoever** — silent. Consistent with
feature bit 3 being off. No NAK is sent either. This is the only
command that is genuinely "fire-and-forget" — by virtue of being
ignored.

### 2.4 `GET_SETTINGS` (CMD 0x10)

**Inconsistent and probably misdocumented in the FSD.**

Swept IDs `0x00..0x14` with the camera in standby:

| IDs tested | Result |
|---|---|
| 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x11, 0x13 | NAK `0x04` (invalid ID) followed by 3 spurious NAK `0x01` frames queued |
| 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14 | silent, no response |

The pattern is unstable across runs (in v25 the parity-relation was the
opposite). What's stable: **none of the IDs from the FSD §4.5 table
(resolution=0x01, fps=0x02, etc.) returned valid settings data**. The
FSD's setting-ID map appears to be invented; it does not match this
camera.

**Recommendation:** treat `GET_SETTINGS` / `WRITE_SETTING` as **not
implemented in firmware** until either (a) RunCam publishes the
Thumb Pro W setting-ID map, or (b) you systematically reverse-engineer
it by sniffing the official RunCam app's UART traffic. The web UI
Settings tab should be disabled — none of its sliders will actually
do anything against this camera.

### 2.5 No camera-initiated traffic seen

Across 35+ seconds of pure listening (multiple runs) the camera emitted
**no unsolicited frames**, including no `REQUEST_FC_ATTITUDE (0x50)`.
The optional FC-attitude support documented in §5.6 of the protocol doc
is irrelevant for this firmware.

---

## 3. Bug catalogue — production firmware needs these fixes

Listed in dependency order. My earlier (spec-only) report had four
items here; I have corrected two of them that turned out to be wrong
after seeing the actual wire traffic.

### 3.1 Critical — response parser only handles one format

**File:** `src/protocol/runcam_protocol.cpp::parseResponse`

Current parser assumes `[0xCC][CMD][payload][CRC]` for every response.
The real protocol has **two** response formats: `[0xCC][...]` for
`GET_DEVICE_INFO` only, and `[0x55][LEN][STATUS][...][CRC]` for
everything else. Need to dispatch on the first byte received.

```cpp
// proposed shape
enum class RespKind { DeviceInfo, Ack, Nak, Unknown };
struct ParsedResponse {
    RespKind kind;
    uint8_t  protoVer;
    uint16_t features;
    uint8_t  actionEcho;
    uint8_t  errCode;
};
ParseResult parseResponse(const uint8_t* data, size_t len, ParsedResponse& out);
```

### 3.2 Critical — `parseDeviceInfo` extracts the wrong fields

**File:** same.

Current code reads `payload[0]` as `protocolVersion`, `payload[1]` as
`modelId`, `payload[2]` as `featureBitmask`. Real layout is:

```
data[0] = 0xCC
data[1] = protocolVersion       ← not "cmd echo"
data[2] = feat_lo               ← was being read as modelId
data[3] = feat_hi               ← was being read as featureBitmask
data[4] = CRC over data[0..3]
```

`modelId` does not exist. Drop the field.

### 3.3 Critical — `featureBitmask` is `uint8_t`, should be `uint16_t`

**File:** `src/protocol/runcam_protocol.h::DeviceInfo`,
`src/camera/runcam_camera.h::featureBitmask_`.

Camera reports a 16-bit mask. Thumb Pro W happens to use only the low
byte (`0x0077`) so it works by accident today, but the upper byte is
discarded.

### 3.4 Critical — `CAMERA_CONTROL` waits for the wrong response shape

**File:** `src/camera/runcam_camera.cpp::sendCommand`.

After sending `CAMERA_CONTROL`, the camera **does** reply — with a
0x55-framed ACK or NAK, **not** with a 0xCC-framed response. The
current code treats anything that doesn't begin with `0xCC` as
`ERROR_HEADER` and retries 3 × 500 ms before giving up. Result: every
button-press takes 1.5 s and reports `ERROR_TIMEOUT` even though the
camera ACK'd in <10 ms.

Fix: in `sendCommand`, route the response through the new dispatching
parser from §3.1. On ACK, return `CameraResult::OK`. On NAK, return a
new code `CameraResult::CAMERA_REJECTED` carrying the err byte.

### 3.5 Important — protocol-spec CRC table has a typo

The protocol doc says `GET_DEVICE_INFO` is `CC 00 DB`. The correct CRC
of `CC 00` under DVB-S2 is `0x60`. The other five vectors in that table
all match — only this one is wrong. The v22 sketch hit this and flagged
the CRC implementation as broken; v23 corrected the expected value.
**Document the corrected vector** in `runcam_thumb_pro_w_protocol.md`
§4 and in the unit-test fixtures.

### 3.6 Important — toggle recording via action 0x01, not 0x03/0x04

Empirically, action `0x03` (Start) is unreliable in real states (always
NAK'd 0x02 in our tests). Action `0x01` (Power) **always ACKs** and
toggles recording the same way the hardware button does. Update
`RunCamCamera::startRecording()` / `stopRecording()` to send action
`0x01` and track internal state, instead of `0x03`/`0x04`.

If you keep the existing 0x03/0x04 calls (e.g. because a future
firmware fixes it), at minimum gate them behind the feature bits **and**
treat NAK 0x02 as "fall back to 0x01".

### 3.7 Important — `GET_SETTINGS` does not work as the FSD describes

**File:** `src/camera/runcam_camera.cpp::writeSetting / readSetting`
and `src/web/index.html` Settings tab.

The FSD §4.5 setting-ID map does not match this camera. Until the real
map is known, disable the settings tab in the web UI (or mark every
control as "Not supported"). `PreflightCheck` should also be disabled —
it relies on `readSetting()` to compare against NVS, and the camera
returns NAK or silence for all settings IDs we've tried.

### 3.8 Minor — `parseResponse` length math assumes a cmd-echo byte

`size_t payloadLen = len - 3` assumed `header + cmd + crc` overhead.
The real overhead is **2 bytes** for `GET_DEVICE_INFO` responses
(header + crc, no cmd echo) and **3 bytes** for 0x55-format ACK/NAK
(header + length + crc). Pick the right overhead per format inside
the dispatch.

---

## 4. Test methodology and what each iteration learned

Six diagnostic sketches were flashed and run. Each was reviewed and the
next one written to drill into a specific question. Full logs are in
`/tmp/diag*.log` if needed.

| Iter | Question | Answer |
|---|---|---|
| v22 | Does the camera respond at all? Does CRC self-test pass? | CRC algo correct; spec's `CC 00 DB` vector is wrong (real value `0x60`). Camera silent at first — was in QR mode. |
| v23 | Wider probe: baud sweep, CRC variants, legacy 0x55 protocol | Camera responds at 115200 (the right baud), gives garbage at other bauds, and produces `55 ..` error frames for malformed requests — eventually returned `CC 01 77 00 02` for a correct command. |
| v24 | Apply newly-found format. Does the response really not have 0xCC? | Responses **do** start with 0xCC for GET_DEVICE_INFO and **0x55** for everything else. ACK/NAK structure decoded. Start recording NAK'd. |
| v25 | Mode-cycle to Video and retry recording. Sweep settings IDs. | Mode cycling stable. Start still NAK'd. Settings IDs return NAK 0x04 or silence; FSD map is wrong. |
| v26 | Try Power (0x01) as record toggle. Test in every mode. Wider CMD sweep. | Power always ACKs, Photo always ACKs, Start never ACKs. Confirms recording should be toggled with Power, not Start. |

---

## 5. Recommended next steps

1. **Apply the fixes in §3** (in dependency order). The minimum set to
   get end-to-end functionality of recording + device info is 3.1, 3.2,
   3.3, 3.4, 3.6. The unit-test fixtures in `tests/test_protocol_parsing.cpp`
   and `tests/test_protocol_encoding.cpp` need to be rewritten against
   the real response formats before any of this can be re-verified by
   host tests.
2. **Update the protocol reference doc** with the correct `CC 00 60`
   vector and the real response formats (both the `[0xCC]`-led one
   and the `[0x55]`-led one with status/length).
3. **Update the FSD** sections 4.1, 4.4, 4.5, and 4.7 to match
   reality. §4.5 (settings ID table) is unrecoverable until vendor
   docs are obtained.
4. **Settings + preflight: park them.** Mark the Settings web-UI tab as
   disabled and remove the preflight call until §3.7 has a real
   solution. Leave the preflight scaffolding in place for when
   settings access becomes possible.
5. **Restore `src/main.cpp`.** The current contents are diagnostic v26;
   the production `main.cpp` from Phase 4 was overwritten by the
   debugging effort and needs to be reinstated from git history
   (the project is not in git per the workspace metadata, so check
   `Coder_Summary_Phase4_*.md` for what it contained, or regenerate
   from the FSD). Keep v26 as `tests/serial_diagnostic.cpp` or similar
   for future hardware debugging.

---

## 6. The diagnostic sketch (v26) — preserved location

`src/main.cpp` currently contains the v26 diagnostic. It is the most
useful tool we have for this hardware. Recommended actions:

- **Move it** to a non-build location, e.g.
  `scripts/serial_diagnostic_v26.cpp`, **before** restoring the real
  `main.cpp`. Document its build steps (it compiles standalone — drop
  it into `src/main.cpp`, `pio run -t upload`, monitor).
- It already understands both response formats, so future protocol
  debugging can re-use it as-is.

---

## 7. Appendix — empirical wire traces

### 7.1 Successful `GET_DEVICE_INFO` exchange

```
TX  CC 00 60                          (3 bytes)
RX  CC 01 77 00 02                    (5 bytes)
        │  │  │  └── CRC8/DVB-S2 over CC 01 77 00 = 0x02 ✓
        │  └──┴────── features = 0x0077 (LE)
        └─────────── proto v1
```

### 7.2 ACK example — Mode (action 0x02)

```
TX  CC 01 02 4D                       (4 bytes)
RX  55 06 01 00 02 63                 (6 bytes)
    │  │  │  │  │  └── CRC = 0x63 ✓
    │  │  │  │  └───── action echo = 0x02 (Mode)
    │  │  │  └──────── reserved = 0x00
    │  │  └─────────── status = 0x01 (OK)
    │  └────────────── length = 6
    └───────────────── header = 0x55
```

### 7.3 NAK example — Start recording while already recording

```
TX  CC 01 03 98                       (4 bytes)
RX  55 05 FF 02 1A                    (5 bytes)
    │  │  │  │  └── CRC = 0x1A ✓
    │  │  │  └───── err_code = 0x02 (wrong state)
    │  │  └──────── status = 0xFF (NAK)
    │  └─────────── length = 5
    └────────────── header = 0x55
```

### 7.4 NAK example — GET_SETTINGS with bad ID

```
TX  CC 10 01 5C
RX  55 05 FF 04 9B  55 05 FF 01 B0  55 05 FF 01 B0  55 ...
    └── first frame: NAK err=0x04 (bad ID)
                       └── plus three queued NAK err=0x01 frames
                           (camera retransmits when it thinks it sees
                            additional garbage on the line)
```

The trailing `0x01` NAKs are spurious — they don't correspond to any
fresh command on our side. They look like the camera's reaction to
extra characters it perceives, possibly an internal retry mechanism.
The fix from the firmware's point of view is just to drain them.

---

*End of report.*
