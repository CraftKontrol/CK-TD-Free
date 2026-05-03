# DMX4ALL USB Interface Plugin for TouchDesigner
## Version 1.001
### Author: Arnaud Cassone © Artcraft Visuals

A TouchDesigner CPlusPlus DAT plugin that drives a **DMX4ALL USB/Serial interface** directly from a Table DAT, using the native Win32 serial API (no third-party drivers needed beyond the virtual COM port from the DMX4ALL device).

---

## Requirements

- **Windows** x64
- **TouchDesigner** 2022+ (tested on 2025.32460)
- **DMX4ALL USB interface** with FTDI VCP driver installed  
  (the device must appear as a COM port in Device Manager)
- **Visual Studio 2022** (v143 toolset) — only needed if rebuilding the DLL

---

## Installation

1. Copy the folder `Dmx4AllUsb/` anywhere accessible (e.g. your project folder or a shared library path).
2. In TouchDesigner, add a **CPlusPlus DAT** node.
3. Set **Plugin Path** to `bin/Release/Dmx4AllDat.dll`.
4. The node will show a custom **Dmx4all** parameter page.

---

## Parameters

| Parameter | Type   | Description |
|-----------|--------|-------------|
| **COM Port** | String | Windows COM port name, e.g. `COM3`. Supports ports > COM9 automatically (`\\.\COM10`). |
| **Connect** | Pulse  | Toggle connection. First pulse opens the port and runs a `C?` handshake. Second pulse closes it (sends Blackout before disconnecting). |
| **Blackout** | Toggle | Sends `B1` to the interface (all channels off at the fixture level). Does **not** clear the internal universe buffer. Disable to resume output. |

---

## Input DAT (optional)

Connect a Table DAT with DMX values. Two layouts are supported:

| Layout | Rows | Cols | Description |
|--------|------|------|-------------|
| A | 512 | 1 | Row index = DMX channel − 1. Value in column 0. |
| B | 1 | 512 | Column index = DMX channel − 1. Value in row 0. |

Values are clamped to integers 0–255. If no input is connected, the last sent universe is displayed.

---

## Output Table

The DAT outputs a 513-row × 2-column table:

| Row | Col 0 | Col 1 |
|-----|-------|-------|
| 0 | `channel` | `value` |
| 1–512 | DMX channel number (1-indexed) | Current value (0–255) |

---

## Info CHOP Channels

| Channel | Description |
|---------|-------------|
| `connected` | 1 when port is open, 0 otherwise |
| `frames_sent` | Number of universe sends since last connect |
| `bytes_sent` | Total bytes written to the serial port |

---

## Protocol Notes

The plugin uses the **DMX4ALL Array Transfer** protocol (not ASCII per-channel):

- **Write packet:** `0xFF  start_low  start_high  count  data...`
- The 512-channel universe is sent as **3 packets** per cook:
  - Channels 1–255 (start = 0)
  - Channels 256–510 (start = 255)
  - Channels 511–512 (start = 510, count = 2)
- Serial settings: **38400 baud, 8N1, no handshake**
- The interface acknowledges each packet with ASCII `G`

---

## Quickstart

1. Plug in the DMX4ALL USB interface — note its COM port in Device Manager.
2. In TD, set **COM Port** to `COM3` (or your port).
3. Press **Connect** — the node turns green if the handshake succeeds.
4. Connect a Table CHOP → DAT or a constant Table DAT (512 rows × 1 col, values 0–255).
5. The plugin cooks every frame while connected and pushes the universe to the interface.

---

## Rebuilding the DLL

Open `Dmx4AllDat.sln` in Visual Studio 2022 and build **Release | x64**.  
Output: `bin/Release/Dmx4AllDat.dll`

The project only uses Windows SDK headers (`windows.h`) and the two TD SDK headers (`CPlusPlus_Common.h`, `DAT_CPlusPlusBase.h`) which are included in the folder.  
No additional dependencies or NuGet packages are required.

---

## Known Issues

- The plugin cooks every frame **only when connected** (`cookEveryFrameIfAsked = true`). If nothing downstream uses the DAT, TD may not cook it — add a Null DAT viewer or connect it to an Execute DAT to force cooking in Perform mode.
- Strict DMX4ALL hardware may reject block-transfer packet 2 (start=255, count=255) because `start + count = 510 > 255`. If channels 256–510 are not updating, switch to per-channel ASCII mode (see `SetChannel()` in `Dmx4AllDat.cpp`).
- Windows only. macOS is not supported (Win32 serial API).
