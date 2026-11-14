# Mayavi

BLE HID emulation and keystroke framework for the ESP32-S3, with an on-board
Wi-Fi dashboard and a serial console.

Mayavi turns the board into a configurable Bluetooth Low Energy HID device: it
advertises as a keyboard, mouse, gamepad, media remote or TV box, pairs with a
host, and types programmable keystroke payloads from a browser served by the
device itself.

**Use it only on hardware you own or have written permission to test.**

---

## Board

| | |
|---|---|
| Module | ESP32-S3-N16R8, 16 MB QIO flash, 8 MB octal PSRAM |
| Console port | native USB (`303a:1001`, `/dev/ttyACM0`) |
| Status LED | WS2812 on GPIO 48 (`v1.1` boards use GPIO 38 — edit `platformio.ini`) |
| Framework | Arduino, PlatformIO `espressif32` |

## Build and flash

```sh
pio run -e mayavi                     # firmware
pio run -e mayavi -t upload           # + flash it
pio run -e mayavi -t uploadfs         # web assets from data/ -> LittleFS
```

Flash both: the firmware without a filesystem serves a "LittleFS has no
index.html" page. If the port is ambiguous, pin it:
`--upload-port /dev/ttyACM0`.

## Using it

1. Power the board. It raises an access point:
   **SSID `Mayavi-XXXX`** (MAC suffix), **open — no password**.
2. Open **http://192.168.4.1**.
3. *Connect* tab → pick a profile → **Start discovery**.
4. On the target, pair the device. Windows 10/11 shows a Swift Pair toast;
   macOS/Linux list it by name.
5. When the pill turns **ready**, go to *Payloads* and press **Run payload**.

Everything is also available over serial at 115200 baud.

## Profiles

| id | appearance | advertised as |
|---|---|---|
| `keyboard` | 0x03C1 | Wireless Keyboard |
| `mouse` | 0x03C2 | Wireless Mouse |
| `gamepad` | 0x03C4 | Wireless Gamepad |
| `joystick` | 0x03C3 | Wireless Joystick |
| `pen` | 0x03C7 | Digital Pen |
| `android-tv` | 0x0280 | Android TV |
| `apple-tv` | 0x0280 | Apple TV |
| `airtag` | 0x0200 | AirTag |
| `jiotag` | 0x0200 | JioTag |
| `hrm` | 0x0341 | HR Sensor |
| `custom` | user set | user set name |

Switching a profile changes the advertised name and appearance only. The GATT
table is one composite HID device — keyboard (report 1), mouse (2), gamepad (3),
consumer control (4) — so no re-pairing is needed when the profile changes.

## Serial console

```
1 / start [sec]     start BLE discovery advertising
0 / stop            stop advertising / disconnect the host
2 / status          connection, security, payload state
3 / profiles        list emulated profiles
profile <id>        switch profile
name <text>         change the advertised name
appearance <hex>    raw appearance code, selects the custom profile
duration <sec>      discovery timeout, 5..60
mode swift|beacon   Swift Pair or plain advertisement
forget              clear stored Bluetooth bonds
4 / hid             payload menu
5 / hello           Notepad + Hello test payload
payload begin       enter a script, finish with END
payload show        print the stored script
payload run         execute it
abort               stop and release all keys
wifi                show the dashboard access point
wifi open           drop the password and reboot into an open AP
wifi set <s> <p>    change it, then reboot (empty password = open)
reboot
```

## Payload syntax

```
REM comment                     # comment
STRING Hello from Mayavi
DELAY 500
ENTER / TAB / ESC / BACKSPACE / DELETE / SPACE
UP / DOWN / LEFT / RIGHT / HOME / END / F1..F12
GUI r                           CTRL SHIFT ESC
DEFAULTDELAY 30                 # per-character gap, ms
```

The runner is a non-blocking state machine, so the dashboard stays responsive
and **Stop** releases keys immediately.

## Layout

```
src/main.cpp      boot, access point, status LED, loop
src/settings.*    NVS-backed identity + AP credentials
src/emul.*        BLE stack, HID device, advertising payloads, bonds
src/hid.*         report descriptor, key tables, report senders
src/payload.*     parser + cooperative runner + presets
src/web.*         LittleFS assets + JSON API
src/console.*     serial CLI
data/             dashboard (index.html, app.js, style.css)
```

## Notes and limits

- Advertising budget is 31 bytes. In Swift Pair mode the name inside the
  manufacturer payload is clipped to 18 bytes; the full name (25 max) goes in
  the scan response.
- The access point and BLE share the 2.4 GHz radio. Coexistence works; heavy
  dashboard polling during a payload can add a little jitter.
- Tracker profiles (`airtag`, `jiotag`) emulate the advertised appearance and
  name only — they cannot register with a real Find My network.
- Pairing is Just Works with bonding (`ESP_LE_AUTH_REQ_SC_BOND`), which is what
  a keyboard-shaped HID device normally does. `forget` clears the bonds so the
  host pairs again.
- The AP ships open so the dashboard can be reached without setup. Anyone in
  radio range can therefore drive the HID side — set a password from the
  Settings tab (or `wifi set`) for anything but a bench test.
- Two USB ports: the serial console is on the **native** USB port (CDC). The
  CH343 bridge port works for flashing but only carries the ROM bootloader
  output, not the CLI.

---

<div align="center">
  <sub>Engineered with precision for secure, scalable systems.</sub><br/>
  <b>Made by Saransh Singh</b>
</div>
