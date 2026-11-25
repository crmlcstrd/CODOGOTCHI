#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "face.h"

const int TOUCH_PIN = 4;
const uint32_t FRAME_MS = 33;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);

class OledCanvas : public Canvas {
public:
  void clear() override {
    oled.clearDisplay();
  }

  void fillRoundRect(int x, int y, int w, int h, int r, bool on) override {
    oled.fillRoundRect(x, y, w, h, r, on ? SSD1306_WHITE : SSD1306_BLACK);
  }

  void fillTriangle(int x0, int y0, int x1, int y1,
                    int x2, int y2, bool on) override {
    oled.fillTriangle(x0, y0, x1, y1, x2, y2,
                      on ? SSD1306_WHITE : SSD1306_BLACK);
  }

  void drawLine(int x0, int y0, int x1, int y1, bool on) override {
    oled.drawLine(x0, y0, x1, y1, on ? SSD1306_WHITE : SSD1306_BLACK);
  }
};

OledCanvas canvas;
Face face;

int health = 60;
uint32_t lastFrame = 0;

bool isTouched() {
  return digitalRead(TOUCH_PIN) == LOW;
}

void handleSerial(uint32_t now) {
  while (Serial.available()) {
    char ch = Serial.read();

    if (ch >= '0' && ch <= '3') {
      face.setMood(ch - '0');
      Serial.print("Mood set to: ");
      Serial.println(ch);
    }
    else if (ch == 'p' || ch == 'P') {
      face.react(REACT_PET, now);
      Serial.println("Pet reaction!");
    }
    else if (ch == 'c' || ch == 'C') {
      face.react(REACT_CELEBRATE, now);
      Serial.println("Celebration!");
    }
    else if (ch == '+') {
      health += 10;
      if (health > 100) health = 100;
      face.setHealth(health);
      Serial.print("Health: ");
      Serial.println(health);
    }
    else if (ch == '-') {
      health -= 10;
      if (health < 0) health = 0;
      face.setHealth(health);
      Serial.print("Health: ");
      Serial.println(health);
    }
  }
}

void scanI2C() {
  Serial.println("Scanning I2C...");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);

    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      Serial.println(address, HEX);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(TOUCH_PIN, INPUT_PULLUP);

  Wire.begin();
  scanI2C();

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed!");
    while (true);
  }

  oled.clearDisplay();
  oled.fillScreen(SSD1306_WHITE);
  oled.display();
  delay(600);

  face.seed(micros() ^ 0x9E3779B9u);
  face.setMood(MOOD_NEUTRAL);
  face.setHealth(health);

  Serial.println();
  Serial.println("Desk buddy ready!");
  Serial.println("0 = Sad");
  Serial.println("1 = Neutral");
  Serial.println("2 = Slightly happy");
  Serial.println("3 = Happy");
  Serial.println("p = Pet");
  Serial.println("c = Celebrate");
  Serial.println("+ = Increase health");
  Serial.println("- = Decrease health");
}

void loop() {
  uint32_t now = millis();

  handleSerial(now);

  if (isTouched()) {
    face.react(REACT_PET, now);
  }

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;

    face.update(now);
    face.render(canvas);

    oled.display();
  }
}
