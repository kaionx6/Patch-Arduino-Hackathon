/*
  ESP32-C6 onboard ARGB binary word blinker.

  Sends each character of a word as 8-bit ASCII, most-significant bit first.
  Default word: "Patch"

  For many ESP32-C6 dev boards the onboard addressable RGB LED is on GPIO 8.
  If your board uses a different pin, change LED_PIN below.
*/

#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

const uint8_t LED_PIN = LED_BUILTIN;

const uint8_t ONE_RED = 0;
const uint8_t ONE_GREEN = 45;
const uint8_t ONE_BLUE = 0;

const uint8_t ZERO_RED = 0;
const uint8_t ZERO_GREEN = 0;
const uint8_t ZERO_BLUE = 35;

const uint16_t BIT_ON_MS = 220;
const uint16_t BIT_OFF_MS = 140;
const uint16_t LETTER_GAP_MS = 650;
const uint16_t WORD_GAP_MS = 1800;

String word = "Patch";

void setLed(uint8_t red, uint8_t green, uint8_t blue) {
  neopixelWrite(LED_PIN, red, green, blue);
}

void ledOff() {
  setLed(0, 0, 0);
}

void blinkBit(bool bitValue) {
  if (bitValue) {
    setLed(ONE_RED, ONE_GREEN, ONE_BLUE);
  } else {
    setLed(ZERO_RED, ZERO_GREEN, ZERO_BLUE);
  }

  delay(BIT_ON_MS);
  ledOff();
  delay(BIT_OFF_MS);
}

void blinkCharacter(char character) {
  uint8_t ascii = static_cast<uint8_t>(character);

  Serial.print(character);
  Serial.print(" = ");

  for (int8_t bit = 7; bit >= 0; bit--) {
    bool bitValue = bitRead(ascii, bit);
    Serial.print(bitValue ? '1' : '0');
    blinkBit(bitValue);
  }

  Serial.println();
  delay(LETTER_GAP_MS);
}

void readSerialWord() {
  if (!Serial.available()) {
    return;
  }

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input.length() > 0) {
    word = input;
    Serial.print("New word: ");
    Serial.println(word);
  }
}

void blinkWord() {
  Serial.print("Blinking word: ");
  Serial.println(word);

  for (size_t index = 0; index < word.length(); index++) {
    blinkCharacter(word[index]);
    readSerialWord();
  }

  delay(WORD_GAP_MS);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  ledOff();

  Serial.println("ESP32-C6 binary word blinker");
  Serial.println("1 bits blink green, 0 bits blink blue.");
  Serial.println("Type a word in Serial Monitor and press Enter to change it.");
}

void loop() {
  readSerialWord();
  blinkWord();
}
