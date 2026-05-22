# RunCam Thumb Pro W — Serial (UART) Protocol Reference

> **Purpose:** Complete protocol reference for implementing a test/control program to interface with the RunCam Thumb Pro W (SKU: THUMB-PRO-W) over UART.
> **Audience:** Developer or LLM implementing serial communication code.
> **Sources:** RunCam official protocol spec, Betaflight `rcdevice.h`, ArduPilot `AP_RunCam.cpp/h`, community implementations, **plus empirical verification against a physical Thumb Pro W on 2026-05-21 (see `Serial_Diagnostic_Report.md`).**
>
> **Several sections in this doc had errors that were corrected after on-the-wire testing.** The corrected sections are marked **[verified on hardware]**. Earlier published versions of this doc claimed responses had no header byte, that `CAMERA_CONTROL` was fire-and-forget, and listed `CC 00 DB` as the GET_DEVICE_INFO test vector — all three were wrong.

---

## 1. Hardware Interface

### Connector

The Thumb Pro W exposes a **1.25 mm 4-pin JST connector** with the following pinout (looking at the connector face on the camera body):

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | 5 V DC | Power input, 5 V nominal |
| 2 | GND | Common ground |
| 3 | TX | Camera transmits on this pin (connect to host RX) |
| 4 | GND | Second ground (connect to host GND) |

> **Note:** The camera's TX line carries data *from* the camera. To send commands *to* the camera, connect the host TX line to the camera's UART RX pin. Some pinout diagrams label pin 4 as RX; verify with a multimeter or oscilloscope if in doubt, as cable assemblies vary.

### UART Configuration

| Parameter | Value |
|-----------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None |
| Logic level | 3.3 V (5 V tolerant on most hardware, but use level shifter to be safe) |

---

## 2. Protocol Overview

The Thumb Pro W uses the **RunCam Device Protocol v1.0** (also called "RC Device Protocol"). This is a binary framed protocol over UART. It is **not** the older legacy RC Split protocol (which used header `0x55` and trailer `0xAA`).

### Key characteristics

- All multi-byte integers are **little-endian**.
- Every packet starts with the fixed header byte `0xCC`.
- Every packet ends with a **CRC-8** checksum byte (DVB-S2 polynomial).
- The camera must be **fully booted** before it accepts commands (allow ~3–5 seconds after power-on).
- The Thumb Pro W is a **2-key style** device (like the Split family). It does **not** support the 5-key OSD cable simulation commands.
- Camera settings are changed by **simulating button presses** to navigate the on-screen menu — there is no direct read/write command for individual settings.

---

## 3. Packet Structure

### Host → Camera (command packet)

```
┌──────────┬──────────┬──────────────────────┬──────────┐
│  HEADER  │  CMD ID  │  DATA (0–N bytes)    │   CRC8   │
│  0xCC    │  1 byte  │  variable            │  1 byte  │
└──────────┴──────────┴──────────────────────┴──────────┘
```

- **HEADER**: Always `0xCC`.
- **CMD ID**: Command identifier byte (see Section 5).
- **DATA**: Zero or more payload bytes depending on the command.
- **CRC8**: Computed over all preceding bytes in the packet (HEADER + CMD ID + DATA).

### Camera → Host (response packet) — **[verified on hardware]**

Response packets **do** include a leading header byte. There are two distinct response formats:

**Format A — `GET_DEVICE_INFO` response only** (5 bytes):

```
┌──────────┬──────────┬──────────────────────┬──────────┐
│  HEADER  │  PROTO   │  FEATURES (LE)       │   CRC8   │
│  0xCC    │  1 byte  │  2 bytes             │  1 byte  │
└──────────┴──────────┴──────────────────────┴──────────┘
```

**Format B — every other response** (5 or 6 bytes):

```
ACK  (6 bytes)
┌──────────┬──────────┬──────────┬──────────┬─────────────────┬──────────┐
│  HEADER  │  LENGTH  │  STATUS  │  0x00    │  ACTION_ECHO    │   CRC8   │
│  0x55    │  0x06    │  0x01    │  1 byte  │  1 byte         │  1 byte  │
└──────────┴──────────┴──────────┴──────────┴─────────────────┴──────────┘

NAK  (5 bytes)
┌──────────┬──────────┬──────────┬─────────────────┬──────────┐
│  HEADER  │  LENGTH  │  STATUS  │  ERR_CODE       │   CRC8   │
│  0x55    │  0x05    │  0xFF    │  1 byte         │  1 byte  │
└──────────┴──────────┴──────────┴─────────────────┴──────────┘
```

The CRC is computed over **all preceding bytes including the header** (DVB-S2, same algorithm as commands).

**NAK error codes observed on hardware:**

| Code | Meaning |
|------|---------|
| `0x01` | Unknown command or wrong CRC |
| `0x02` | Wrong camera state for command (e.g. `START_RECORDING` when already recording) |
| `0x04` | Invalid argument or unsupported setting ID |

> **Implementation note:** When parsing a response, dispatch on the first byte: `0xCC` = device-info format (5 bytes total), `0x55` = ACK/NAK format (length is in byte 1). Do not assume a fixed response length.

---

## 4. CRC-8 Algorithm

The checksum algorithm is **CRC-8/DVB-S2** (polynomial `0xD5`, initial value `0x00`, no reflection, no final XOR).

### C implementation

```c
uint8_t crc8_dvb_s2(uint8_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x80) {
            crc = (crc << 1) ^ 0xD5;
        } else {
            crc = (crc << 1);
        }
    }
    return crc;
}

/* Compute CRC over an entire buffer */
uint8_t compute_crc(const uint8_t *buf, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc = crc8_dvb_s2(crc, buf[i]);
    }
    return crc;
}
```

### Python implementation

```python
def crc8_dvb_s2(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0xD5) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc
```

### Packet construction example

```python
def build_packet(cmd_id: int, data: bytes = b'') -> bytes:
    payload = bytes([0xCC, cmd_id]) + data
    crc = crc8_dvb_s2(payload)
    return payload + bytes([crc])
```

### Verified CRC test vectors — **[corrected]**

These can be used to validate your CRC implementation. **The first row was previously listed as `CC 00 DB`; that was wrong** — the correct CRC over `CC 00` under DVB-S2 is `0x60`, verified against the camera (which rejects `CC 00 DB` and accepts `CC 00 60`):

| Packet description | Raw bytes (hex) |
|--------------------|-----------------|
| GET_DEVICE_INFO | `CC 00 60` |
| CAMERA_CONTROL: Simulate WiFi button | `CC 01 00 32` |
| CAMERA_CONTROL: Simulate Power button | `CC 01 01 E7` |
| CAMERA_CONTROL: Simulate Change Mode | `CC 01 02 4D` |
| CAMERA_CONTROL: Start Recording | `CC 01 03 98` |
| CAMERA_CONTROL: Stop Recording | `CC 01 04 CC` |

---

## 5. Command Reference

### 5.1 GET_DEVICE_INFO — `0x00` — **[verified on hardware]**

Query the camera for its protocol version and supported feature bitmask. **Always send this first** to confirm the camera is alive and to learn which features it supports.

**Request packet:** 3 bytes

```
CC 00 60
```

(`0x60` = CRC-8/DVB-S2 over `CC 00`. Older copies of this doc listed `DB` — incorrect.)

**Response packet:** 5 bytes — **starts with `0xCC` header**

```
┌──────────┬─────────────────────┬────────────────────────────┬──────────┐
│  HEADER  │  PROTOCOL_VERSION   │  FEATURES (uint16_t, LE)   │   CRC8   │
│  0xCC    │  1 byte             │  2 bytes                   │  1 byte  │
└──────────┴─────────────────────┴────────────────────────────┴──────────┘
```

CRC8 is computed over the first 4 bytes (header + proto + 2-byte features).

**Verified response from a Thumb Pro W:**

```
CC 01 77 00 02
│  │  │  │  └── CRC = DVB-S2 over CC 01 77 00 = 0x02
│  │  └──┴────── features uint16_t LE = 0x0077
│  └──────────── protocol version = 1
└─────────────── header
```

Wait **up to 500 ms** for the response after sending. If no response, retry up to 3 times.

**PROTOCOL_VERSION values:**

| Value | Meaning |
|-------|---------|
| `0x00` | Legacy RC Split firmware ≤1.1.0 |
| `0x01` | RC Device Protocol v1.0 (current) |

**FEATURES bitmask (uint16_t, bits 0–7 used):**

| Bit | Mask | Feature constant | Description |
|-----|------|-----------------|-------------|
| 0 | `0x0001` | `FEATURE_SIMULATE_POWER_BUTTON` | Power button simulation |
| 1 | `0x0002` | `FEATURE_SIMULATE_WIFI_BUTTON` | WiFi button simulation |
| 2 | `0x0004` | `FEATURE_CHANGE_MODE` | Mode change (video/photo/OSD) |
| 3 | `0x0008` | `FEATURE_SIMULATE_5_KEY_OSD_CABLE` | 5-key OSD cable (NOT on Thumb Pro) |
| 4 | `0x0010` | `FEATURE_DEVICE_SETTINGS_ACCESS` | Direct settings read/write |
| 5 | `0x0020` | `FEATURE_DISPLAYPORT` | DisplayPort OSD |
| 6 | `0x0040` | `FEATURE_START_RECORDING` | Explicit start recording command |
| 7 | `0x0080` | `FEATURE_STOP_RECORDING` | Explicit stop recording command |

The Thumb Pro W reports features `0x0077` (bits 0, 1, 2, 4, 5, 6 set) — verified on hardware. This means: power, WiFi, mode change, settings access, displayport, and START_RECORDING are advertised. **Bit 3 (5-key OSD) is not set** and **bit 7 (STOP_RECORDING) is not set**. There is no separate stop-recording command — recording is stopped by re-sending Start (toggling) or by the Power button.

> **Caveat:** in practice the explicit `START_RECORDING` action (`0x03`) returns NAK `0x02` ("wrong state") on this firmware even in valid states. Use the **Power button (action `0x01`)** to toggle recording. See §5.2.

---

### 5.2 CAMERA_CONTROL — `0x01`

Simulate a physical button press on the camera. This is the primary control command.

**Request packet:** 4 bytes

```
CC 01 <ACTION> <CRC>
```

**ACTION byte values:**

| Action name | ACTION byte | Full packet | Description |
|-------------|-------------|-------------|-------------|
| Simulate WiFi Button | `0x00` | `CC 01 00 32` | Toggle WiFi on/off. In OSD menu: **Confirm/Enter** selection |
| Simulate Power Button | `0x01` | `CC 01 01 E7` | Start/stop video recording. In OSD menu: **Next item** |
| Simulate Change Mode | `0x02` | `CC 01 02 4D` | Switch between video, photo, QR/OSD modes. In OSD menu: **Exit menu** |
| Start Recording | `0x03` | `CC 01 03 98` | Explicit start recording (if FEATURE_START_RECORDING supported) |
| Stop Recording | `0x04` | `CC 01 04 CC` | Explicit stop recording (if FEATURE_STOP_RECORDING supported) |

**Response: [verified on hardware]** — the camera **does** respond to every `CAMERA_CONTROL`. Earlier docs claimed this was fire-and-forget; that is wrong. The response is in the 0x55-framed ACK/NAK format (see §3):

```
ACK : 55 06 01 00 <echoed_action> <CRC>      (6 bytes)
NAK : 55 05 FF <err_code> <CRC>              (5 bytes)
```

**Empirical per-action behaviour on a Thumb Pro W:**

| Action | Result | Notes |
|--------|--------|-------|
| `0x00` WiFi/Confirm | ACK or NAK 0x02 | NAKs in non-menu modes — Confirm has nothing to confirm |
| `0x01` Power | ACK always | Toggles recording / navigates in menu — the reliable record toggle |
| `0x02` Mode | ACK always | Cycles Video → Photo → QR |
| `0x03` Start Recording | NAK 0x02 in our tests | Feature bit advertises support but command appears unreliable |
| `0x04` Stop Recording | ACK when applicable | Feature bit 7 is OFF on the Thumb Pro W; works anyway on this firmware |
| `0x05` Capture Photo | ACK always | |

> **Important:** Wait at least **200 ms** between successive button simulation commands to allow the camera firmware to process each press. Sending commands too rapidly may cause them to be missed.
>
> **Reliable recipe for record start/stop:** use action `0x01` (Power button). It always ACKs and the camera handles the state toggle internally — exactly as if you pressed the physical button.

---

### 5.3 5KEY_SIMULATION_PRESS — `0x02` *(not supported on Thumb Pro W)*

Simulates pressing one of the 5 OSD navigation keys. **The Thumb Pro W does not support this command** (bit 3 of features bitmask is not set). Do not use.

For reference, the key action bytes are:
`0x01`=Enter, `0x02`=Left, `0x03`=Up, `0x04`=Right, `0x05`=Down.

---

### 5.4 5KEY_SIMULATION_RELEASE — `0x03` *(not supported on Thumb Pro W)*

Companion to `5KEY_SIMULATION_PRESS`. Not applicable to the Thumb Pro W.

---

### 5.5 5KEY_CONNECTION — `0x04` *(not supported on Thumb Pro W)*

Opens/closes a 5-key OSD session. Not applicable to the Thumb Pro W.

Sub-command bytes: `0x01`=open connection, `0x02`=close connection.

**Response (when supported):** 3 bytes — `[result_code, action_id, CRC8]`.

---

### 5.6 REQUEST_FC_ATTITUDE — `0x50` *(camera → host)*

This command is initiated **by the camera** (not the host). The camera sends this to request attitude data (roll, pitch, yaw) from the flight controller for OSD overlay purposes. The host (FC) should respond with the attitude data.

**Camera-initiated request:** 2 bytes

```
CC 50 <CRC>
```

**Host response format (if implementing FC-side):** 10 bytes

```
┌────────┬────────┬────────┬────────┬────────┬────────┬──────────┐
│ ROLL   │ ROLL   │ PITCH  │ PITCH  │ YAW    │ YAW    │  CRC8    │
│  low   │  high  │  low   │  high  │  low   │  high  │          │
│ int16_t (LE) × 10 = degrees │        │          │
└────────┴────────┴────────┴────────┴────────┴────────┴──────────┘
```

Each value is a signed 16-bit integer (little-endian), representing angle × 10 (i.e., 123 = 12.3°).

> This command is **optional** for a basic test program. Only implement if you want attitude overlay in the camera OSD.

---

## 6. OSD Menu Navigation

Since there is no direct settings read/write API, camera settings are changed by navigating the on-screen menu using button simulation commands.

### Entering the OSD menu

Send `CAMERA_CONTROL` with ACTION `0x02` (Change Mode) repeatedly until the camera enters OSD/settings mode. The camera cycles: **Video → Photo → QR Code/OSD Settings**.

### In-menu navigation

Once inside the OSD menu, the three button actions change meaning:

| Action | Normal mode | In OSD menu |
|--------|-------------|-------------|
| `0x00` (WiFi) | Toggle WiFi | **Confirm / Enter** current selection |
| `0x01` (Power) | Start/stop recording | **Move to next item** |
| `0x02` (Change Mode) | Switch camera mode | **Exit menu / Back** |

### Navigation sequence example (change a setting)

```
1. CC 01 02 4D  → Enter OSD mode (may need to press several times)
2. CC 01 01 E7  → Next item (navigate to desired menu entry)
3. CC 01 01 E7  → Next item
4. CC 01 00 32  → Confirm/Enter (enter submenu or change value)
5. CC 01 01 E7  → Next value option
6. CC 01 00 32  → Confirm selection
7. CC 01 02 4D  → Exit menu
```

> **Note:** There is no way to read the current OSD menu state over serial. Navigation is blind — you must track state in software based on how many Next/Enter commands have been sent.

---

## 7. Complete Packet Examples

All values in hexadecimal. CRC is the last byte.

```
Query device info:          CC 00 DB
Toggle WiFi (or confirm):   CC 01 00 32
Start/stop recording:       CC 01 01 E7
Switch mode (or exit OSD):  CC 01 02 4D
Start recording (explicit): CC 01 03 98
Stop recording (explicit):  CC 01 04 CC
```

---

## 8. Timing and State Machine Notes

| Situation | Recommended delay |
|-----------|------------------|
| After power-on, before first command | ≥ 3000 ms (camera boot time) |
| Between successive button commands | ≥ 200 ms |
| Timeout waiting for GET_DEVICE_INFO response | 500 ms (retry up to 3×) |
| After entering OSD mode | ≥ 300 ms before next command |

---

## 9. Suggested Test Program Structure

```
1. Open UART at 115200 8N1
2. Wait 3000 ms for camera boot
3. Send GET_DEVICE_INFO (CC 00 DB)
4. Read 5-byte response, parse protocol version and features
5. Print features bitmask
6. Send CAMERA_CONTROL start recording (CC 01 03 98)
7. Wait 5000 ms
8. Send CAMERA_CONTROL stop recording (CC 01 04 CC)
9. Print "done"
```

---

## 10. Reference Implementations

These open-source codebases implement this protocol and are useful for cross-reference:

| Project | File | Notes |
|---------|------|-------|
| Betaflight | `src/main/io/rcdevice.h` | Command and feature defines |
| Betaflight | `src/main/io/rcdevice.c` | Protocol state machine |
| ArduPilot | `libraries/AP_Camera/AP_RunCam.cpp` | Full FC-side implementation |
| ArduPilot | `libraries/AP_Camera/AP_RunCam.h` | Command enum, feature flags |

Official RunCam protocol specification (may require login):  
`https://support.runcam.com/hc/en-us/articles/360014537794-RunCam-Device-Protocol`

---

## 11. LED Indicator States and Current Draw

The camera has a single **bi-colour LED** (red/green) that indicates operating state. This is the primary out-of-band status signal available to the host system.

### LED state table

| LED state | Camera state | Notes |
|-----------|-------------|-------|
| **Off** | Unpowered, or booting (transient) | See boot sequence below |
| **Solid red** | **Standby / ready** — booted, idle, waiting for command | UART commands accepted in this state |
| **Slow blinking red** | **Recording video** | SD card write active |
| **Fast blinking red** | **SD card error** — card missing, full, too slow, or damaged | Camera is alive but cannot record; replace or reformat card |
| **Solid green** | **QR code / parameter settings mode** | Entered by double-clicking the button; camera waits to scan a settings QR code from the RunCam app |
| **Slow blinking green** | **Firmware update in progress** (early phase) | Do not power off; do not send UART commands |
| **Fast blinking green** | **Firmware update completing** | Camera will shut off automatically when done |

### Current draw by state (measured at 5 V supply)

| Current | LED state | Camera state |
|---------|-----------|-------------|
| ~0 A | Off / transitioning | Booting — SoC not yet fully initialised; UART not ready |
| ~0.25 A | Solid red | Standby — fully booted, idle, ready for commands |
| ~0.32 A | Slow blinking red | Recording — video encoder, SD write controller active |

> **Note:** The ~70 mA increase during recording is caused by the video encoder and SD card write pipeline. Higher-resolution or higher-framerate modes may draw slightly more.

### Boot sequence and UART readiness

```
Power applied
    │
    ├─ ~0 A, LED off ──── booting (2–4 seconds) ────────────────┐
    │                                                             │
    └─ ~0.25 A, LED solid red ── standby, UART ready ◄──────────┘
                │
                │  (button press or UART command CC 01 03 98)
                ▼
        ~0.32 A, LED slow blinking red ── recording
                │
                │  (button press or UART command CC 01 04 CC)
                ▼
        ~0.25 A, LED solid red ── standby again
```

**Do not send any UART commands until the LED is solid red.** The camera ignores all input during boot.

### Using current draw as a recording confirmation

Because the camera sends no acknowledgement for `CAMERA_CONTROL` commands, monitoring supply current is a useful hardware-level confirmation of recording state, independent of the serial protocol. A simple INA219 or similar current-sense IC on the 5 V rail can confirm:

- Solid red + ~0.25 A → standby confirmed
- Blinking red + ~0.32 A → recording confirmed
- Fast red flash → SD card problem; recording command will not succeed

### SD card error state

If the camera boots without a card, or the card is full/faulty, the LED flashes red rapidly. In this state:
- The camera is otherwise alive and will respond to `GET_DEVICE_INFO`
- It will **not** start recording regardless of commands sent
- Resolve by inserting a working card and power-cycling the camera

---

## 12. Common Pitfalls — **[updated from hardware testing]**

- **Wrong CRC algorithm:** The legacy RC Split protocol (also header `0x55`) uses a different CRC polynomial (`0x31`). The Thumb Pro W uses DVB-S2 (`0xD5`) for **both commands and responses**, including the 0x55-framed ACK/NAK responses. Do not switch CRC algorithms based on the header byte.
- **Assuming CAMERA_CONTROL has no response:** It does — a 5- or 6-byte 0x55-framed reply. Reading and discarding that reply is harmless, but **timing out and retrying** (because you assumed no response) wastes ~1.5 s per command.
- **Sending commands before boot:** The camera needs ~3 seconds after power-on; the LED must be solid red before sending any command. Sending commands immediately after power-on will be ignored.
- **Recording confirmation over UART:** Use the ACK from action `0x01` (Power) as the protocol-level signal that the toggle was accepted. Use LED state or supply current to confirm the *physical* state change. The explicit Start/Stop (0x03/0x04) actions are unreliable on this firmware.
- **SD card errors block recording:** A fast-flashing red LED means recording will not start regardless of commands sent. Check the card.
- **Rapid-fire commands:** Spacing commands at least 200 ms apart is required for reliable operation.
- **5-key commands:** These will be silently ignored by the Thumb Pro W. Always check the feature bitmask first.
- **Response parsing:** Response packets **do** start with a header byte: `0xCC` for `GET_DEVICE_INFO` (5 bytes total), `0x55` for everything else (length-prefixed). Dispatch on the first byte received.
- **Multi-frame responses:** When a request causes a NAK, the camera occasionally retransmits the same error frame 3–4 times back-to-back. Allow at least 150 ms of inter-byte timeout, and accept that one logical reply may span multiple 0x55 frames in the receive buffer.
- **Setting IDs are not standardised:** The setting-ID map for `GET_SETTINGS` / `WRITE_SETTING` is firmware- and model-specific. There is no publicly documented map for the Thumb Pro W; sniff the official RunCam app's UART traffic if you need this.
- **CRC in response:** The response CRC is DVB-S2 over the **entire response frame including the header byte** (`0xCC` or `0x55`), excluding only the CRC byte itself.

---

*End of document*
