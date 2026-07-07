# Patch-Arduino-Hackathon

The best Hackathon team ever consisting of
Dónal Ó Conalláin, Deepta Suresh, Conall O'Reilly, Kelvin Gao

Winner Repo btw

hjello

## ESP32-C6 cute robot with PlatformIO + ESP-IDF

Open this repo in VS Code with the PlatformIO extension installed.

### L298N Wiring

- `ENA`: GPIO `2`
- `IN1`: GPIO `3`
- `IN2`: GPIO `0`
- `IN3`: GPIO `1`
- `IN4`: GPIO `6`
- `ENB`: GPIO `7`

The motors dance differently depending on the mood received over Bluetooth LE.

Use the separate 9V battery for the motor supply, and connect the L298N ground,
ESP32-C6 ground, and battery negative together.

### LCD Wiring

The 16-pin LCD is wired in 4-bit mode and shows the robot eyes: `U       U`.

- LCD pin `1` VSS: GND
- LCD pin `2` VDD: 5V
- LCD pin `3` VO: potentiometer middle pin
- LCD pin `4` RS: GPIO `10`
- LCD pin `5` RW: GND
- LCD pin `6` E: GPIO `11`
- LCD pins `7-10`: not connected
- LCD pin `11` D4: GPIO `4`
- LCD pin `12` D5: GPIO `5`
- LCD pin `13` D6: GPIO `15`
- LCD pin `14` D7: GPIO `23`
- LCD pin `15` backlight +: 5V
- LCD pin `16` backlight -: GND

### MAX7219 Dot Matrix Wiring

The MAX7219 dot matrix shows the robot mouth as a `D` rotated 90 degrees
clockwise. The code compensates for the module being mounted upside down.

- `DIN`: GPIO `18`
- `CLK`: GPIO `19`
- `CS`: GPIO `20`
- `VCC`: 5V
- `GND`: GND

### Bluetooth Mood Control

The ESP32-C6 advertises as a Bluetooth LE device:

- Device name: `LoopyBot`
- Service UUID: `0100646f-6f6d-2d74-6f62-2d79706f6f6c`
- Mood characteristic UUID: `0200646f-6f6d-2d74-6f62-2d79706f6f6c`

Supported moods:

- `happy`
- `sad`
- `angry`
- `moody`
- `joyful`

Connect to `LoopyBot` with a BLE app such as nRF Connect, find the mood
characteristic, and write one of the mood words as UTF-8 text.

You can also open `ble_mood_sender.html` in Chrome, connect to `LoopyBot`, and
click the mood buttons.

### Upload

1. Plug in the ESP32-C6.
2. In VS Code, open the PlatformIO sidebar.
3. Select `esp32-c6-devkitc-1` as the environment.
4. Click `Build`.
5. Click `Upload`.
6. Click `Monitor` to view the motor direction logs.

If upload gets stuck at connecting, hold the board's `BOOT` button while
clicking `Upload`, then release it once flashing starts.
