# Patch-Arduino-Hackathon

The best Hackathon team ever consisting of
Dónal Ó Conalláin, Deepta Suresh, Conall O'Reilly, Kelvin Gao

Winner Repo btw

hjello

## ESP32-C6 binary word blinker with PlatformIO + ESP-IDF

Open this repo in VS Code with the PlatformIO extension installed.

- Default word: `Patch`
- `1` bits blink green
- `0` bits blink blue
- Send a new word in the PlatformIO serial monitor at `115200` baud to change
  what it blinks

The sketch assumes the onboard ARGB LED is on GPIO `8`, which is common on
ESP32-C6 dev boards. If your board uses a different LED pin, change `LED_GPIO`
in `src/main.c`.

### Upload

1. Plug in the ESP32-C6.
2. In VS Code, open the PlatformIO sidebar.
3. Select `esp32-c6-devkitc-1` as the environment.
4. Click `Build`.
5. Click `Upload`.
6. Click `Monitor` to view the binary output and type new words.

If upload gets stuck at connecting, hold the board's `BOOT` button while
clicking `Upload`, then release it once flashing starts.
